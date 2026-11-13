/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   A.I. Disclosure:  These unit tests written by Anthropic Claude.
   -----------------------------------------------------------------------------
   Poly voice assigner tests (design-voice-architecture.md section 5.1).

   Driven through synth_handle_midi() - MIDI -> keys layer -> assigner ->
   voices - checking the assigner's decisions on the voices.
   -----------------------------------------------------------------------------
*/

#include <string.h>

#include "midi_parser.h"
#include "patch.h"
#include "synth.h"
#include "unity.h"

static struct synth s;

static void send(uint8_t status, uint8_t d1, uint8_t d2) {
  synth_handle_midi(&s, status, d1, d2);
}
static void note_on(uint8_t n) { send(MIDI_STATUS_NOTE_ON, n, 100); }
static void note_off(uint8_t n) { send(MIDI_STATUS_NOTE_OFF, n, 0); }
static void cc(uint8_t c, uint8_t v) { send(MIDI_STATUS_CONTROL_CHANGE, c, v); }

static void render_n(int n) {
  float l[AUDIO_BLOCK_SIZE], r[AUDIO_BLOCK_SIZE];
  while (n--) {
    synth_render(&s, l, r);
  }
}

static struct voice *voice_for(uint8_t note) {
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    if (s.voice[i].state == VOICE_ACTIVE && s.voice[i].note == note) {
      return &s.voice[i];
    }
  }
  return NULL;
}

static int count_state(enum voice_state state) {
  int n = 0;
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    n += s.voice[i].state == state;
  }
  return n;
}

/* Default patch, poly, with the given release so voices can be in release */
static void load_patch(float release) {
  param_value_t p[PARAM_COUNT];
  param_create_default_patch(p);
  p[DCA_RELEASE].f = release;
  synth_load_patch(&s, p);
  render_n(1);
}

/* Hold one key per voice, starting at `first`, one block apart */
static void fill_voices(uint8_t first) {
  for (uint8_t i = 0; i < SYNTH_MAX_VOICES; i++) {
    note_on(first + 2 * i);
    render_n(1);
  }
}

void setUp(void) {
  synth_init(&s, MIDI_OMNI, NULL);
  load_patch(0.0f);
}

void tearDown(void) {}

/* ------------------------------------------------------------------------- */

void poly_is_the_assigner_for_the_default_patch(void) {
  TEST_ASSERT_EQUAL_PTR(&poly_assigner, s.assigner);
}

void each_new_note_takes_an_idle_voice(void) {
  fill_voices(60);
  TEST_ASSERT_EQUAL_INT(SYNTH_MAX_VOICES, count_state(VOICE_ACTIVE));

  for (uint8_t i = 0; i < SYNTH_MAX_VOICES; i++) {
    TEST_ASSERT_NOT_NULL(voice_for(60 + 2 * i));
  }
}

/* D4: a note still sounding is retriggered on the same voice, not doubled */
void repress_of_a_releasing_note_reuses_its_voice(void) {
  load_patch(0.5f); /* long release */
  note_on(60);
  render_n(2);
  note_off(60);
  render_n(2);
  struct voice *v = voice_for(60);
  TEST_ASSERT_NOT_NULL(v);
  TEST_ASSERT_FALSE(v->gate);

  note_on(60);
  TEST_ASSERT_EQUAL_INT(1, count_state(VOICE_ACTIVE));
  TEST_ASSERT_TRUE(v->gate);
  TEST_ASSERT_EQUAL(ENV_ATTACK, v->amp_env.state);
}

/* Tier 1: a releasing voice is stolen first - the quietest of them */
void steal_takes_the_quietest_releasing_voice(void) {
  load_patch(0.5f);
  fill_voices(60);
  note_off(64); /* released first: quieter by the time we steal */
  render_n(20);
  note_off(66);
  render_n(2);

  note_on(90);

  struct voice *v = NULL;
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    if (s.voice[i].state == VOICE_STEALING) {
      v = &s.voice[i];
    }
  }
  TEST_ASSERT_NOT_NULL(v);
  TEST_ASSERT_EQUAL_UINT8(64, v->note);
  TEST_ASSERT_EQUAL_UINT8(90, v->target_note);
}

/*
 * Tier 2: a note held only by a pedal is stolen before one whose key is
 * down.  Any pedal counts - here the sostenuto pedal.
 */
void steal_takes_a_sostenuto_held_note_before_a_key_down(void) {
  fill_voices(60);
  cc(MIDI_CC_SOSTENUTO, 127); /* captures every sounding note */
  note_off(66);               /* key up: now held only by the pedal */

  note_on(90);

  struct voice *v = NULL;
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    if (s.voice[i].state == VOICE_STEALING) {
      v = &s.voice[i];
    }
  }
  TEST_ASSERT_NOT_NULL(v);
  TEST_ASSERT_EQUAL_UINT8(66, v->note); /* not 60, the oldest key down */
}

/*
 * C9: "oldest" must survive any number of notes.  The old int8_t age
 * wrapped after 127 notes, so a long-held note stopped looking oldest.
 */
void oldest_is_still_stolen_after_hundreds_of_notes(void) {
  note_on(30); /* the drone - oldest from here on */
  render_n(1);

  for (int i = 0; i < 300; i++) { /* short notes on the other voices */
    note_on(40 + (i % 5));
    render_n(1);
    note_off(40 + (i % 5));
    render_n(2);
  }

  for (uint8_t i = 1; i < SYNTH_MAX_VOICES; i++) { /* fill the rest, held */
    note_on(60 + 2 * i);
    render_n(1);
  }
  TEST_ASSERT_EQUAL_INT(SYNTH_MAX_VOICES, count_state(VOICE_ACTIVE));

  note_on(90); /* every key is down: the oldest must go */

  struct voice *drone = NULL;
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    if (s.voice[i].note == 30) {
      drone = &s.voice[i];
    }
  }
  TEST_ASSERT_NOT_NULL(drone);
  TEST_ASSERT_EQUAL(VOICE_STEALING, drone->state);
}

/* No nested steals: a voice mid-steal is never chosen again */
void a_voice_mid_steal_is_not_stolen_again(void) {
  fill_voices(60);
  note_on(90); /* steals the oldest (60) */
  note_on(91); /* must take a different voice */

  TEST_ASSERT_EQUAL_INT(2, count_state(VOICE_STEALING));
  int targets = 0;
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    if (s.voice[i].state == VOICE_STEALING) {
      targets |= (s.voice[i].target_note == 90) ? 1 : 2;
    }
  }
  TEST_ASSERT_EQUAL_INT(3, targets);
}

/* With every voice mid-steal, a further note has nowhere to go */
void a_note_is_dropped_when_every_voice_is_mid_steal(void) {
  fill_voices(60);
  for (uint8_t i = 0; i < SYNTH_MAX_VOICES; i++) {
    note_on(90 + i);
  }
  TEST_ASSERT_EQUAL_INT(SYNTH_MAX_VOICES, count_state(VOICE_STEALING));

  note_on(110); /* dropped, no crash */
  TEST_ASSERT_EQUAL_INT(SYNTH_MAX_VOICES, count_state(VOICE_STEALING));
}

/* The stolen note's later key-up must not touch the voice's new note */
void key_up_of_a_stolen_note_does_not_release_the_new_note(void) {
  fill_voices(60);
  note_on(90); /* steals 60 */
  render_n(10);
  struct voice *v = voice_for(90);
  TEST_ASSERT_NOT_NULL(v);

  note_off(60);
  TEST_ASSERT_TRUE(v->gate);
  TEST_ASSERT_NOT_EQUAL(ENV_RELEASE, v->amp_env.state);
}

/* A gate-off for a note still mid-steal is applied when it starts */
void gate_off_mid_steal_is_applied_when_the_note_starts(void) {
  load_patch(0.5f);
  fill_voices(60);
  note_on(90);
  note_off(90); /* inside the ~10ms fade */

  render_n(10);
  struct voice *v = voice_for(90);
  TEST_ASSERT_NOT_NULL(v);
  TEST_ASSERT_EQUAL(ENV_RELEASE, v->amp_env.state);
}

/*
 * K1: a continuous sostenuto pedal sends repeated values above 64.  Only
 * the first may capture - a note played after it must not be held.
 */
void repeated_sostenuto_values_do_not_capture_later_notes(void) {
  note_on(48);
  cc(MIDI_CC_SOSTENUTO, 127);
  note_on(64);
  cc(MIDI_CC_SOSTENUTO, 100); /* same pedal, still down */
  render_n(2);

  note_off(64);
  struct voice *v = voice_for(64);
  TEST_ASSERT_NOT_NULL(v);
  TEST_ASSERT_FALSE(v->gate);
}

void all_off_silences_every_voice(void) {
  fill_voices(60);
  s.assigner->all_off(&s);
  TEST_ASSERT_EQUAL_INT(SYNTH_MAX_VOICES, count_state(VOICE_IDLE));
}

void poly_has_no_unison(void) {
  param_value_t p = {.f = 1.0f};
  synth_set_param(&s, SYNTH_UNISON_DETUNE, p);
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, s.voice[i].unison_cents);
    TEST_ASSERT_TRUE(s.voice[i].start_phase < 0.0f);
  }
}

void poly_mix_scale_is_unchanged(void) {
  TEST_ASSERT_EQUAL_FLOAT(0.4564f, s.assigner->mix_scale(&s));
}

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(poly_is_the_assigner_for_the_default_patch);
  RUN_TEST(each_new_note_takes_an_idle_voice);
  RUN_TEST(repress_of_a_releasing_note_reuses_its_voice);
  RUN_TEST(steal_takes_the_quietest_releasing_voice);
  RUN_TEST(steal_takes_a_sostenuto_held_note_before_a_key_down);
  RUN_TEST(oldest_is_still_stolen_after_hundreds_of_notes);
  RUN_TEST(a_voice_mid_steal_is_not_stolen_again);
  RUN_TEST(a_note_is_dropped_when_every_voice_is_mid_steal);
  RUN_TEST(key_up_of_a_stolen_note_does_not_release_the_new_note);
  RUN_TEST(gate_off_mid_steal_is_applied_when_the_note_starts);
  RUN_TEST(repeated_sostenuto_values_do_not_capture_later_notes);
  RUN_TEST(all_off_silences_every_voice);
  RUN_TEST(poly_has_no_unison);
  RUN_TEST(poly_mix_scale_is_unchanged);

  return UNITY_END();
}
