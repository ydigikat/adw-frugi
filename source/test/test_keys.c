/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   A.I. Disclosure:  These unit tests written by Anthropic Claude.
   -----------------------------------------------------------------------------
   Keys layer tests (design-voice-architecture.md section 4).

   Events are recorded as a short text log so each test reads as the expected
   sequence:  "D60/100" key_down(60, vel 100), "U60" key_up(60),
   "G60" gate_off(60).
   -----------------------------------------------------------------------------
*/

#include <stdio.h>
#include <string.h>

#include "keys.h"
#include "unity.h"

static struct keys keys;
static char events[1024];

static void log_event(const char *text) {
  if (events[0]) {
    strncat(events, " ", sizeof(events) - strlen(events) - 1);
  }
  strncat(events, text, sizeof(events) - strlen(events) - 1);
}

static void on_key_down(void *ctx, uint8_t note, uint8_t velocity) {
  char buf[16];
  TEST_ASSERT_EQUAL_PTR(&keys, ctx);
  snprintf(buf, sizeof(buf), "D%u/%u", note, velocity);
  log_event(buf);
}

static void on_key_up(void *ctx, uint8_t note) {
  char buf[16];
  TEST_ASSERT_EQUAL_PTR(&keys, ctx);
  snprintf(buf, sizeof(buf), "U%u", note);
  log_event(buf);
}

static void on_gate_off(void *ctx, uint8_t note) {
  char buf[16];
  TEST_ASSERT_EQUAL_PTR(&keys, ctx);
  snprintf(buf, sizeof(buf), "G%u", note);
  log_event(buf);
}

static const struct keys_events handlers = {
    .key_down = on_key_down, .key_up = on_key_up, .gate_off = on_gate_off};

/* Check and clear the log */
#define EXPECT_EVENTS(expected)                                                \
  do {                                                                         \
    TEST_ASSERT_EQUAL_STRING((expected), events);                              \
    events[0] = '\0';                                                          \
  } while (0)

void setUp(void) {
  events[0] = '\0';
  keys_init(&keys, &handlers, &keys);
}

void tearDown(void) {}

/* -------------------------------------------------------------------------
 * Basics and the gate rule
 * ---------------------------------------------------------------------- */

void starts_with_nothing_down_or_gated(void) {
  TEST_ASSERT_FALSE(keys_any_down(&keys));
  TEST_ASSERT_FALSE(keys_any_gated(&keys));
  TEST_ASSERT_EQUAL_UINT8(KEYS_NONE, keys_last_down(&keys));
}

void key_down_sends_key_down_and_gates_the_note(void) {
  keys_key_down(&keys, 60, 100);
  EXPECT_EVENTS("D60/100");

  TEST_ASSERT_TRUE(keys_is_down(&keys, 60));
  TEST_ASSERT_TRUE(keys_is_gated(&keys, 60));
  TEST_ASSERT_EQUAL_UINT8(100, keys_velocity(&keys, 60));
}

void key_up_without_pedals_sends_key_up_then_gate_off(void) {
  keys_key_down(&keys, 60, 100);
  keys_key_up(&keys, 60);
  EXPECT_EVENTS("D60/100 U60 G60");

  TEST_ASSERT_FALSE(keys_is_gated(&keys, 60));
}

void key_up_for_a_key_that_is_not_down_is_ignored(void) {
  keys_key_up(&keys, 60);
  EXPECT_EVENTS("");
}

/* A repeated note-on without a note-off is a fresh press */
void repeated_key_down_is_sent_again(void) {
  keys_key_down(&keys, 60, 100);
  keys_key_down(&keys, 60, 50);
  EXPECT_EVENTS("D60/100 D60/50");
  TEST_ASSERT_EQUAL_UINT8(50, keys_velocity(&keys, 60));

  keys_key_up(&keys, 60);
  EXPECT_EVENTS("U60 G60");
}

void notes_outside_the_midi_range_are_ignored(void) {
  keys_key_down(&keys, 128, 100);
  keys_key_up(&keys, 200);
  EXPECT_EVENTS("");
  TEST_ASSERT_FALSE(keys_is_gated(&keys, 128));
  TEST_ASSERT_FALSE(keys_any_down(&keys));
}

void works_with_no_event_handlers(void) {
  keys_init(&keys, NULL, NULL);
  keys_key_down(&keys, 60, 100);
  keys_sustain(&keys, true);
  keys_key_up(&keys, 60);
  keys_sustain(&keys, false);
  TEST_ASSERT_FALSE(keys_any_gated(&keys));
}

/* -------------------------------------------------------------------------
 * The worked examples from design section 4.4, verbatim
 * ---------------------------------------------------------------------- */

void example_sustain_and_repress(void) {
  keys_sustain(&keys, true);
  EXPECT_EVENTS("");

  keys_key_down(&keys, 60, 100); /* 60:[D] */
  EXPECT_EVENTS("D60/100");

  keys_key_up(&keys, 60); /* 60:[S]  still gated */
  EXPECT_EVENTS("U60");
  TEST_ASSERT_TRUE(keys_is_gated(&keys, 60));

  keys_key_down(&keys, 60, 100); /* 60:[D]  S cleared */
  EXPECT_EVENTS("D60/100");

  keys_sustain(&keys, false); /* still gated by D: no gate_off */
  EXPECT_EVENTS("");
  TEST_ASSERT_TRUE(keys_is_gated(&keys, 60));

  keys_key_up(&keys, 60); /* 60:[] */
  EXPECT_EVENTS("U60 G60");
}

void example_sostenuto_holds_only_what_was_sounding(void) {
  keys_key_down(&keys, 48, 100); /* 48:[D] */
  keys_sostenuto(&keys, true);   /* 48:[D C] */
  keys_key_down(&keys, 64, 100); /* 64:[D]  not captured */
  EXPECT_EVENTS("D48/100 D64/100");

  keys_key_up(&keys, 48); /* 48:[C] */
  keys_key_up(&keys, 64); /* 64:[] */
  EXPECT_EVENTS("U48 U64 G64");
  TEST_ASSERT_TRUE(keys_is_gated(&keys, 48));

  keys_sostenuto(&keys, false); /* 48:[] */
  EXPECT_EVENTS("G48");
}

void example_both_pedals(void) {
  keys_key_down(&keys, 60, 100);
  keys_sustain(&keys, true);
  keys_sostenuto(&keys, true); /* 60:[D C] */
  keys_key_up(&keys, 60);      /* 60:[S C] */
  EXPECT_EVENTS("D60/100 U60");

  keys_sustain(&keys, false); /* 60:[C]  still gated */
  EXPECT_EVENTS("");
  TEST_ASSERT_TRUE(keys_is_gated(&keys, 60));

  keys_sostenuto(&keys, false); /* 60:[] */
  EXPECT_EVENTS("G60");
}

/* -------------------------------------------------------------------------
 * Sustain pedal
 * ---------------------------------------------------------------------- */

/* Unlike sostenuto, sustain holds notes played after it went down */
void sustain_holds_notes_played_after_it_went_down(void) {
  keys_sustain(&keys, true);
  keys_key_down(&keys, 60, 100);
  keys_key_up(&keys, 60);
  EXPECT_EVENTS("D60/100 U60");
  TEST_ASSERT_TRUE(keys_is_gated(&keys, 60));
}

void lifting_sustain_releases_every_held_note_once_in_note_order(void) {
  keys_sustain(&keys, true);
  keys_key_down(&keys, 67, 100);
  keys_key_down(&keys, 60, 100);
  keys_key_down(&keys, 64, 100);
  keys_key_up(&keys, 67);
  keys_key_up(&keys, 60);
  keys_key_up(&keys, 64);
  events[0] = '\0';

  keys_sustain(&keys, false);
  EXPECT_EVENTS("G60 G64 G67");
  TEST_ASSERT_FALSE(keys_any_gated(&keys));
}

void sustain_lifted_before_release_does_not_hold(void) {
  keys_sustain(&keys, true);
  keys_sustain(&keys, false);
  keys_key_down(&keys, 60, 100);
  keys_key_up(&keys, 60);
  EXPECT_EVENTS("D60/100 U60 G60");
}

/* A continuous pedal sends many values each side of 64 - only changes act */
void repeated_sustain_positions_are_ignored(void) {
  keys_sustain(&keys, true);
  keys_key_down(&keys, 60, 100);
  keys_key_up(&keys, 60);
  keys_sustain(&keys, true); /* repeat: no change */
  EXPECT_EVENTS("D60/100 U60");

  keys_sustain(&keys, false);
  keys_sustain(&keys, false); /* repeat: no second gate_off */
  EXPECT_EVENTS("G60");
}

/* -------------------------------------------------------------------------
 * Sostenuto pedal
 * ---------------------------------------------------------------------- */

/* Notes held by sustain at the moment sostenuto goes down are captured */
void sostenuto_captures_notes_held_by_sustain(void) {
  keys_sustain(&keys, true);
  keys_key_down(&keys, 60, 100);
  keys_key_up(&keys, 60); /* 60:[S] */
  keys_sostenuto(&keys, true);
  keys_sustain(&keys, false); /* 60:[C] still gated */
  EXPECT_EVENTS("D60/100 U60");
  TEST_ASSERT_TRUE(keys_is_gated(&keys, 60));

  keys_sostenuto(&keys, false);
  EXPECT_EVENTS("G60");
}

/* D5: a note already released, held by nothing, is not captured */
void sostenuto_does_not_capture_released_notes(void) {
  keys_key_down(&keys, 60, 100);
  keys_key_up(&keys, 60);
  events[0] = '\0';

  keys_sostenuto(&keys, true);
  keys_sostenuto(&keys, false);
  EXPECT_EVENTS("");
}

/*
 * Only the change to "down" captures.  Re-applying it for each repeated
 * pedal value would also capture notes played since the pedal went down.
 */
void repeated_sostenuto_down_does_not_capture_later_notes(void) {
  keys_key_down(&keys, 48, 100);
  keys_sostenuto(&keys, true);
  keys_key_down(&keys, 64, 100);
  keys_sostenuto(&keys, true); /* repeat while 64 is down */
  events[0] = '\0';

  keys_key_up(&keys, 64);
  EXPECT_EVENTS("U64 G64"); /* 64 not captured */
}

/* A re-pressed captured note is still held when released again */
void repress_of_a_captured_note_keeps_the_capture(void) {
  keys_key_down(&keys, 48, 100);
  keys_sostenuto(&keys, true);
  keys_key_up(&keys, 48);
  keys_key_down(&keys, 48, 90);
  keys_key_up(&keys, 48);
  EXPECT_EVENTS("D48/100 U48 D48/90 U48");
  TEST_ASSERT_TRUE(keys_is_gated(&keys, 48));

  keys_sostenuto(&keys, false);
  EXPECT_EVENTS("G48");
}

/* Lifting sostenuto while the key is still down: no gate_off yet */
void sostenuto_up_with_key_still_down_does_not_release(void) {
  keys_key_down(&keys, 48, 100);
  keys_sostenuto(&keys, true);
  keys_sostenuto(&keys, false);
  EXPECT_EVENTS("D48/100");
  TEST_ASSERT_TRUE(keys_is_gated(&keys, 48));

  keys_key_up(&keys, 48);
  EXPECT_EVENTS("U48 G48");
}

/* -------------------------------------------------------------------------
 * gate_off exactly once
 * ---------------------------------------------------------------------- */

void gate_off_is_sent_once_however_the_holds_end(void) {
  /* 60: sustain then sostenuto lifted; 62: sostenuto then sustain lifted */
  keys_key_down(&keys, 60, 100);
  keys_key_down(&keys, 62, 100);
  keys_sustain(&keys, true);
  keys_sostenuto(&keys, true);
  keys_key_up(&keys, 60);
  keys_key_up(&keys, 62);
  events[0] = '\0';

  keys_sustain(&keys, false);
  EXPECT_EVENTS("");
  keys_sostenuto(&keys, false);
  EXPECT_EVENTS("G60 G62");

  keys_sustain(&keys, false);
  keys_sostenuto(&keys, false);
  EXPECT_EVENTS("");
}

/* -------------------------------------------------------------------------
 * Bulk operations
 * ---------------------------------------------------------------------- */

void all_keys_up_releases_every_key(void) {
  keys_key_down(&keys, 60, 100);
  keys_key_down(&keys, 64, 100);
  events[0] = '\0';

  keys_all_keys_up(&keys);
  EXPECT_EVENTS("U60 G60 U64 G64");
  TEST_ASSERT_FALSE(keys_any_gated(&keys));
}

/* D2: All Notes Off behaves like releasing the keys - pedals still apply */
void all_keys_up_respects_the_sustain_pedal(void) {
  keys_key_down(&keys, 60, 100);
  keys_sustain(&keys, true);
  events[0] = '\0';

  keys_all_keys_up(&keys);
  EXPECT_EVENTS("U60");
  TEST_ASSERT_TRUE(keys_is_gated(&keys, 60));

  keys_sustain(&keys, false);
  EXPECT_EVENTS("G60");
}

/* CC121: both pedals lifted; keys still down keep sounding */
void release_pedals_frees_held_notes_but_not_keys_still_down(void) {
  keys_key_down(&keys, 60, 100);
  keys_key_down(&keys, 64, 100);
  keys_sustain(&keys, true);
  keys_key_up(&keys, 60);
  events[0] = '\0';

  keys_release_pedals(&keys);
  EXPECT_EVENTS("G60");
  TEST_ASSERT_TRUE(keys_is_gated(&keys, 64));
  TEST_ASSERT_FALSE(keys.sustain_pedal);
  TEST_ASSERT_FALSE(keys.sostenuto_pedal);
}

void reset_forgets_everything_without_events(void) {
  keys_key_down(&keys, 60, 100);
  keys_sustain(&keys, true);
  keys_sostenuto(&keys, true);
  events[0] = '\0';

  keys_reset(&keys);
  EXPECT_EVENTS("");
  TEST_ASSERT_FALSE(keys_any_gated(&keys));
  TEST_ASSERT_FALSE(keys.sustain_pedal);
  TEST_ASSERT_FALSE(keys.sostenuto_pedal);

  keys_key_up(&keys, 60); /* the key's later release is harmless */
  EXPECT_EVENTS("");
}

/* -------------------------------------------------------------------------
 * Queries
 * ---------------------------------------------------------------------- */

/* D7: last-note priority over any number of keys (the old stack held 8) */
void last_down_tracks_the_most_recent_of_many_keys(void) {
  for (uint8_t note = 40; note < 52; note++) { /* 12 keys */
    keys_key_down(&keys, note, 100);
  }
  TEST_ASSERT_EQUAL_UINT8(51, keys_last_down(&keys));

  keys_key_up(&keys, 51);
  TEST_ASSERT_EQUAL_UINT8(50, keys_last_down(&keys));

  keys_key_up(&keys, 45); /* releasing an older key changes nothing */
  TEST_ASSERT_EQUAL_UINT8(50, keys_last_down(&keys));
}

void last_down_follows_press_order_not_pitch(void) {
  keys_key_down(&keys, 72, 100);
  keys_key_down(&keys, 48, 100);
  keys_key_down(&keys, 60, 100);
  TEST_ASSERT_EQUAL_UINT8(60, keys_last_down(&keys));

  keys_key_down(&keys, 72, 100); /* re-press makes it the latest */
  TEST_ASSERT_EQUAL_UINT8(72, keys_last_down(&keys));
}

/* Pedal-held notes are not "down" - mono priority uses keys only (D1) */
void last_down_ignores_notes_held_only_by_a_pedal(void) {
  keys_sustain(&keys, true);
  keys_key_down(&keys, 60, 100);
  keys_key_down(&keys, 64, 100);
  keys_key_up(&keys, 64);

  TEST_ASSERT_EQUAL_UINT8(60, keys_last_down(&keys));
  keys_key_up(&keys, 60);
  TEST_ASSERT_EQUAL_UINT8(KEYS_NONE, keys_last_down(&keys));
  TEST_ASSERT_FALSE(keys_any_down(&keys));
  TEST_ASSERT_TRUE(keys_any_gated(&keys));
}

void notes_at_both_ends_of_the_range(void) {
  keys_key_down(&keys, 0, 1);
  keys_key_down(&keys, 127, 127);
  TEST_ASSERT_TRUE(keys_is_down(&keys, 0));
  TEST_ASSERT_TRUE(keys_is_down(&keys, 127));
  TEST_ASSERT_EQUAL_UINT8(127, keys_last_down(&keys));

  keys_key_up(&keys, 0);
  keys_key_up(&keys, 127);
  EXPECT_EVENTS("D0/1 D127/127 U0 G0 U127 G127");
}

int main(void) {
  UNITY_BEGIN();

  /* Basics and the gate rule */
  RUN_TEST(starts_with_nothing_down_or_gated);
  RUN_TEST(key_down_sends_key_down_and_gates_the_note);
  RUN_TEST(key_up_without_pedals_sends_key_up_then_gate_off);
  RUN_TEST(key_up_for_a_key_that_is_not_down_is_ignored);
  RUN_TEST(repeated_key_down_is_sent_again);
  RUN_TEST(notes_outside_the_midi_range_are_ignored);
  RUN_TEST(works_with_no_event_handlers);

  /* Design section 4.4 worked examples */
  RUN_TEST(example_sustain_and_repress);
  RUN_TEST(example_sostenuto_holds_only_what_was_sounding);
  RUN_TEST(example_both_pedals);

  /* Sustain */
  RUN_TEST(sustain_holds_notes_played_after_it_went_down);
  RUN_TEST(lifting_sustain_releases_every_held_note_once_in_note_order);
  RUN_TEST(sustain_lifted_before_release_does_not_hold);
  RUN_TEST(repeated_sustain_positions_are_ignored);

  /* Sostenuto */
  RUN_TEST(sostenuto_captures_notes_held_by_sustain);
  RUN_TEST(sostenuto_does_not_capture_released_notes);
  RUN_TEST(repeated_sostenuto_down_does_not_capture_later_notes);
  RUN_TEST(repress_of_a_captured_note_keeps_the_capture);
  RUN_TEST(sostenuto_up_with_key_still_down_does_not_release);

  /* gate_off exactly once */
  RUN_TEST(gate_off_is_sent_once_however_the_holds_end);

  /* Bulk operations */
  RUN_TEST(all_keys_up_releases_every_key);
  RUN_TEST(all_keys_up_respects_the_sustain_pedal);
  RUN_TEST(release_pedals_frees_held_notes_but_not_keys_still_down);
  RUN_TEST(reset_forgets_everything_without_events);

  /* Queries */
  RUN_TEST(last_down_tracks_the_most_recent_of_many_keys);
  RUN_TEST(last_down_follows_press_order_not_pitch);
  RUN_TEST(last_down_ignores_notes_held_only_by_a_pedal);
  RUN_TEST(notes_at_both_ends_of_the_range);

  return UNITY_END();
}
