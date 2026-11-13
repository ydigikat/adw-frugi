/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
*/

#include <math.h>
#include <string.h>

#include "keys.h"
#include "map.h"
#include "midi_parser.h"
#include "param.h"
#include "patch.h"
#include "synth.h"
#include "unity.h"
#include "voice.h"

static struct synth s;

/*
 * Fake patch_load for exercising MIDI Program Change without the real
 * EEPROM-backed store (patch_persist_load, which this module never links
 * against - see synth_patch_load_fn in synth.h). One slot resolves to a
 * canned patch; every other slot reports "not found" so the fallback
 * path gets exercised too.
 */
#define FAKE_LOADER_SLOT (5)

static bool fake_patch_load(uint16_t slot, param_value_t params[PARAM_COUNT],
                            char name_out[PATCH_NAME_LEN]) {
  if (slot != FAKE_LOADER_SLOT) {
    return false;
  }

  param_create_default_patch(params);
  params[SYNTH_VOICE_MODE].e = VOICE_MODE_MONO;
  params[SYNTH_PORTAMENTO_ON].b = true;

  if (name_out) {
    strcpy(name_out, "FAKE");
  }

  return true;
}

/* -------------------------------------------------------------------------
 * Fixture setup/teardown
 * ---------------------------------------------------------------------- */
void setUp(void) { synth_init(&s, MIDI_OMNI, fake_patch_load); }

void tearDown(void) {}

/* -----------------------------------------------------------------------
 * Helpers
 * ----------------------------------------------------------------------- */

/* Build a 3-byte channel MIDI message on the stack */
static struct midi_msg make_msg(uint8_t status, uint8_t d1, uint8_t d2) {
  struct midi_msg m;
  m.len = 3;
  m.data[0] = status;
  m.data[1] = d1;
  m.data[2] = d2;
  return m;
}

static struct midi_msg make_note_on(uint8_t note, uint8_t velocity) {
  return make_msg(MIDI_STATUS_NOTE_ON, note, velocity);
}

static struct midi_msg make_note_off(uint8_t note) {
  return make_msg(MIDI_STATUS_NOTE_OFF, note, 0);
}

/* Count voice in a given state */
static int count_voice_in_state(struct synth *synth, enum voice_state state) {
  int count = 0;
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    if (synth->voice[i].state == state)
      count++;
  }
  return count;
}

/* Return the voice playing a note, or NULL */
static struct voice *find_voice_playing(struct synth *synth, uint8_t note) {
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    if (synth->voice[i].state == VOICE_ACTIVE && synth->voice[i].note == note)
      return &synth->voice[i];
  }
  return NULL;
}

/*
 * Pedal state now lives in the keys layer, not on the voice.  These ask it
 * what used to be voice flags: is the note held past its key-up by the
 * sustain pedal / by the sostenuto pedal?
 */
static bool note_bit(const uint8_t set[16], uint8_t note) {
  return (set[note >> 3] & (1u << (note & 7u))) != 0;
}

static bool sustain_holds(uint8_t note) {
  return note_bit(s.keys.sustained, note);
}

static bool sostenuto_holds(uint8_t note) {
  return s.keys.sostenuto_pedal && note_bit(s.keys.captured, note) &&
         !keys_is_down(&s.keys, note);
}

/* Drive synth_render() n times */
static void render_n(struct synth *synth, int n) {
  float unused_samples[AUDIO_BLOCK_SIZE];

  for (int i = 0; i < n; i++)
    synth_render(synth, unused_samples, unused_samples);
}

/* -----------------------------------------------------------------------
 * Initialisation
 * ----------------------------------------------------------------------- */

void test_init_midi_channel_is_omni(void) {
  TEST_ASSERT_EQUAL_UINT8(MIDI_OMNI, s.midi_channel);
}

void test_init_all_voice_idle(void) {
  TEST_ASSERT_EQUAL_INT(SYNTH_MAX_VOICES, count_voice_in_state(&s, VOICE_IDLE));
}

void test_init_params_pointer_set_on_all_voice(void) {
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    TEST_ASSERT_NOT_NULL(s.voice[i].params);
  }
}

/* -----------------------------------------------------------------------
 * Voice allocation — free voice path
 * ----------------------------------------------------------------------- */

void test_note_on_activates_one_voice(void) {
  struct midi_msg m = make_note_on(60, 64);
  synth_handle_midi(&s, m.data[0], m.data[1], m.data[2]);
  TEST_ASSERT_EQUAL_INT(1, count_voice_in_state(&s, VOICE_ACTIVE));
}

void test_note_on_stores_correct_note(void) {
  struct midi_msg m = make_note_on(60, 64);
  synth_handle_midi(&s, m.data[0], m.data[1], m.data[2]);
  TEST_ASSERT_NOT_NULL(find_voice_playing(&s, 60));
}

void test_two_note_on_activates_two_voice(void) {
  struct midi_msg m1 = make_note_on(60, 64);
  struct midi_msg m2 = make_note_on(64, 64);
  synth_handle_midi(&s, m1.data[0], m1.data[1], m1.data[2]);
  synth_handle_midi(&s, m2.data[0], m2.data[1], m2.data[2]);
  TEST_ASSERT_EQUAL_INT(2, count_voice_in_state(&s, VOICE_ACTIVE));
}

void test_eight_note_on_fills_all_voice(void) {
  uint8_t notes[] = {60, 62, 64, 65, 66, 68, 70, 72};
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    struct midi_msg m = make_note_on(notes[i], 64);
    synth_handle_midi(&s, m.data[0], m.data[1], m.data[2]);
  }
  TEST_ASSERT_EQUAL_INT(SYNTH_MAX_VOICES,
                        count_voice_in_state(&s, VOICE_ACTIVE));
}

/* -----------------------------------------------------------------------
 * Voice allocation — retrigger path
 *
 * Sending the same note twice while it is still active should retrigger
 * the envelope, not allocate a second voice.
 * ----------------------------------------------------------------------- */

void test_retrigger_does_not_add_a_second_voice(void) {
  struct midi_msg m = make_note_on(60, 64);
  synth_handle_midi(&s, m.data[0], m.data[1], m.data[2]);
  synth_handle_midi(&s, m.data[0], m.data[1], m.data[2]);
  TEST_ASSERT_EQUAL_INT(1, count_voice_in_state(&s, VOICE_ACTIVE));
}

void test_retrigger_updates_velocity(void) {
  struct midi_msg m1 = make_note_on(60, 64);
  struct midi_msg m2 = make_note_on(60, 100);
  synth_handle_midi(&s, m1.data[0], m1.data[1], m1.data[2]);
  render_n(&s, 1); /* consume START so voice is settled ACTIVE */
  synth_handle_midi(&s, m2.data[0], m2.data[1], m2.data[2]);
  struct voice *v = find_voice_playing(&s, 60);
  TEST_ASSERT_NOT_NULL(v);
  TEST_ASSERT_EQUAL_UINT8(100, v->velocity);
}

/* -----------------------------------------------------------------------
 * Voice allocation — steal path (5th note when all 4 busy)
 * ----------------------------------------------------------------------- */

void test_fifth_note_on_steals_oldest_voice(void) {
  /*
   * Fill all 4 voice, then send a 5th note.  The synth must still have
   * only SYNTH_MAX_VOICES voice in play (one of them now STEALING or already
   * transitioning); none should be left dangling.
   */
  /* Sized for the largest SYNTH_MAX_VOICES across configs (8 in Release,
     6 in Debug/test) - only the first SYNTH_MAX_VOICES entries are used,
     so this stays in bounds either way. */
  uint8_t notes[] = {60, 62, 64, 65, 66, 68, 70, 72};
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    struct midi_msg m = make_note_on(notes[i], 64);
    synth_handle_midi(&s, m.data[0], m.data[1], m.data[2]);
    render_n(&s, 1); /* age each voice */
  }

  struct midi_msg steal = make_note_on(67, 64);
  synth_handle_midi(&s, steal.data[0], steal.data[1], steal.data[2]);

  /* Total voice in any non-idle state must remain <= SYNTH_MAX_VOICES */
  int active = count_voice_in_state(&s, VOICE_ACTIVE);
  int stealing = count_voice_in_state(&s, VOICE_STEALING);
  TEST_ASSERT_LESS_OR_EQUAL_INT(SYNTH_MAX_VOICES, active + stealing);
}

void test_fifth_note_steal_target_is_stealing_state(void) {
  /* After filling 4 voice and sending a 5th, exactly 1 should be STEALING */
  /* Sized for the largest SYNTH_MAX_VOICES across configs (8 in Release,
     6 in Debug/test) - only the first SYNTH_MAX_VOICES entries are used,
     so this stays in bounds either way. */
  uint8_t notes[] = {60, 62, 64, 65, 66, 68, 70, 72};
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    struct midi_msg m = make_note_on(notes[i], 64);
    synth_handle_midi(&s, m.data[0], m.data[1], m.data[2]);
    render_n(&s, 1);
  }

  struct midi_msg steal = make_note_on(67, 64);
  synth_handle_midi(&s, steal.data[0], steal.data[1], steal.data[2]);

  TEST_ASSERT_EQUAL_INT(1, count_voice_in_state(&s, VOICE_STEALING));
}

void test_steal_picks_oldest_not_youngest(void) {
  /*
   * Fill the whole pool oldest-to-youngest, then force a steal. Note 60
   * is allocated first so ages the most (see age_voices()) - it must be
   * the one stolen, not whichever voice was allocated most recently.
   * Sized for the largest SYNTH_MAX_VOICES across configs (8 in Release,
   * 6 in Debug/test) - only the first SYNTH_MAX_VOICES entries are used,
   * so this stays in bounds either way.
   */
  uint8_t notes[] = {60, 62, 64, 65, 66, 68, 70, 72};
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    struct midi_msg m = make_note_on(notes[i], 64);
    synth_handle_midi(&s, m.data[0], m.data[1], m.data[2]);
    render_n(&s, 1);
  }

  struct midi_msg steal = make_note_on(67, 64);
  synth_handle_midi(&s, steal.data[0], steal.data[1], steal.data[2]);

  /* The stolen voice was playing note 60 (the oldest) — its target_note
   * should now be 67; the note field still reads 60 during the RTZ phase. */
  bool found_steal_target = false;
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    if (s.voice[i].state == VOICE_STEALING && s.voice[i].note == notes[0] &&
        s.voice[i].target_note == 67) {
      found_steal_target = true;
    }
  }
  TEST_ASSERT_TRUE(found_steal_target);
}

/* -----------------------------------------------------------------------
 * Voice allocation — steal priority (releasing / sustained voices)
 * ----------------------------------------------------------------------- */

void test_steal_prefers_a_releasing_voice_over_the_objectively_oldest(void) {
  /* Non-zero release time so RELEASE is observable across multiple
     renders instead of collapsing straight to OFF (which would free the
     voice via find_free_voice() before a steal is ever needed). */
  param_value_t release_time = {.f = 0.5f};
  synth_set_param(&s, DCA_RELEASE, release_time);
  render_n(&s, 1); /* apply to all (still idle) voices */

  /* Fill every voice in the pool - SYNTH_MAX_VOICES is 6 in Debug/test, 8
     in Release - so the steal below is genuinely forced through
     find_voice_to_steal() rather than just landing on a free voice. Notes
     array is sized for the largest config either way; only the first
     SYNTH_MAX_VOICES entries are used. 60 (index 0) ends up the
     objectively oldest voice, 62 (index 1) the one that releases early. */
  uint8_t notes[] = {60, 62, 64, 65, 66, 68, 70, 72};
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    struct midi_msg m = make_note_on(notes[i], 64);
    synth_handle_midi(&s, m.data[0], m.data[1], m.data[2]);
    render_n(&s, 1);
  }

  /* 62 releases early - it becomes the youngest voice's note to die, but
     dying is exactly what should make it preferred for stealing, even
     though 60 is the objectively oldest (highest age) voice in the pool. */
  struct midi_msg off62 = make_note_off(62);
  synth_handle_midi(&s, off62.data[0], off62.data[1], off62.data[2]);
  render_n(&s, 1);

  struct voice *releasing_voice = find_voice_playing(&s, 62);
  TEST_ASSERT_NOT_NULL(releasing_voice);
  TEST_ASSERT_EQUAL(ENV_RELEASE, releasing_voice->amp_env.state);

  struct midi_msg steal = make_note_on(67, 64);
  synth_handle_midi(&s, steal.data[0], steal.data[1], steal.data[2]);

  TEST_ASSERT_EQUAL(VOICE_STEALING, releasing_voice->state);
  TEST_ASSERT_EQUAL_UINT8(67, releasing_voice->target_note);
}

void test_steal_prefers_a_sustained_voice_over_a_non_sustained_alternative(
    void) {
  /* Sized for the largest SYNTH_MAX_VOICES across configs (8 in Release,
     6 in Debug/test) - only the first SYNTH_MAX_VOICES entries are used,
     so this stays in bounds either way. */
  uint8_t notes[] = {60, 62, 64, 65, 66, 68, 70, 72};
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    struct midi_msg m = make_note_on(notes[i], 64);
    synth_handle_midi(&s, m.data[0], m.data[1], m.data[2]);
    render_n(&s, 1);
  }

  struct midi_msg pedal_down =
      make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_HOLDPEDAL, 127);
  synth_handle_midi(&s, pedal_down.data[0], pedal_down.data[1],
                    pedal_down.data[2]);

  /* 60 is the objectively oldest voice. Its key has already been released
     and the pedal is just extending it, so it should be the one sacrificed
     ahead of a voice still physically held down at the keyboard. */
  struct midi_msg off60 = make_note_off(60);
  synth_handle_midi(&s, off60.data[0], off60.data[1], off60.data[2]);

  struct voice *held = find_voice_playing(&s, 60);
  TEST_ASSERT_NOT_NULL(held);
  TEST_ASSERT_TRUE(sustain_holds(held->note));

  struct midi_msg steal = make_note_on(67, 64);
  synth_handle_midi(&s, steal.data[0], steal.data[1], steal.data[2]);

  /* The pedal-held voice takes the steal, not a voice whose key is still
     actually held down. */
  TEST_ASSERT_EQUAL(VOICE_STEALING, held->state);
  TEST_ASSERT_EQUAL_UINT8(67, held->target_note);
}

void test_steal_falls_back_to_a_sustained_voice_when_nothing_else_is_available(
    void) {
  /* Sized for the largest SYNTH_MAX_VOICES across configs (8 in Release,
     6 in Debug/test) - only the first SYNTH_MAX_VOICES entries are used,
     so this stays in bounds either way. */
  uint8_t notes[] = {60, 62, 64, 65, 66, 68, 70, 72};
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    struct midi_msg m = make_note_on(notes[i], 64);
    synth_handle_midi(&s, m.data[0], m.data[1], m.data[2]);
    render_n(&s, 1);
  }

  struct midi_msg pedal_down =
      make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_HOLDPEDAL, 127);
  synth_handle_midi(&s, pedal_down.data[0], pedal_down.data[1],
                    pedal_down.data[2]);

  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    struct midi_msg off = make_note_off(notes[i]);
    synth_handle_midi(&s, off.data[0], off.data[1], off.data[2]);
  }

  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    TEST_ASSERT_TRUE(sustain_holds(s.voice[i].note));
  }

  /* Every voice is pedal-held - there's no other candidate pool to draw
     from. The steal must still go through, not silently drop the new
     note. */
  struct midi_msg steal = make_note_on(67, 64);
  synth_handle_midi(&s, steal.data[0], steal.data[1], steal.data[2]);

  TEST_ASSERT_EQUAL_INT(1, count_voice_in_state(&s, VOICE_STEALING));

  /* And it picked the oldest of the (all sustained) candidates - note 60. */
  struct voice *stolen = NULL;
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    if (s.voice[i].state == VOICE_STEALING) {
      stolen = &s.voice[i];
    }
  }
  TEST_ASSERT_NOT_NULL(stolen);
  TEST_ASSERT_EQUAL_UINT8(60, stolen->note);
}

void test_stealing_a_sustained_voice_clears_sustained_for_the_new_note(void) {
  uint8_t notes[] = {60, 62, 64, 65, 66, 68, 70, 72};
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    struct midi_msg m = make_note_on(notes[i], 64);
    synth_handle_midi(&s, m.data[0], m.data[1], m.data[2]);
    render_n(&s, 1);
  }

  struct midi_msg pedal_down =
      make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_HOLDPEDAL, 127);
  synth_handle_midi(&s, pedal_down.data[0], pedal_down.data[1],
                    pedal_down.data[2]);

  /* 60 is the objectively oldest voice - its key is released so the
     pedal takes over, making it the preferred steal target below. */
  struct midi_msg off60 = make_note_off(60);
  synth_handle_midi(&s, off60.data[0], off60.data[1], off60.data[2]);

  struct voice *stolen = find_voice_playing(&s, 60);
  TEST_ASSERT_NOT_NULL(stolen);
  TEST_ASSERT_TRUE(sustain_holds(stolen->note));

  /* Steal it with a brand new note - key held down, pedal still down too */
  struct midi_msg steal = make_note_on(67, 64);
  synth_handle_midi(&s, steal.data[0], steal.data[1], steal.data[2]);
  TEST_ASSERT_EQUAL(VOICE_STEALING, stolen->state);
  TEST_ASSERT_FALSE(sustain_holds(stolen->target_note)); /* the new note */

  /* Let the steal complete so the new note is actually sounding */
  render_n(&s, 50);
  TEST_ASSERT_EQUAL(VOICE_ACTIVE, stolen->state);
  TEST_ASSERT_EQUAL_UINT8(67, stolen->note);

  /* Releasing the pedal must not cut the new note - its key is still down */
  struct midi_msg pedal_up =
      make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_HOLDPEDAL, 0);
  synth_handle_midi(&s, pedal_up.data[0], pedal_up.data[1], pedal_up.data[2]);

  TEST_ASSERT_NOT_EQUAL(ENV_RELEASE, stolen->amp_env.state);
}

void test_note_off_arriving_during_a_steal_is_not_lost(void) {
  /* Force every voice busy and pedal-held, so note 60 (the objectively
     oldest) becomes the preferred steal target - see
     test_steal_prefers_a_sustained_voice_over_a_non_sustained_alternative. */
  uint8_t notes[] = {60, 62, 64, 65, 66, 68, 70, 72};
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    struct midi_msg m = make_note_on(notes[i], 64);
    synth_handle_midi(&s, m.data[0], m.data[1], m.data[2]);
    render_n(&s, 1);
  }

  struct midi_msg pedal_down =
      make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_HOLDPEDAL, 127);
  synth_handle_midi(&s, pedal_down.data[0], pedal_down.data[1],
                    pedal_down.data[2]);

  struct midi_msg off60 = make_note_off(60);
  synth_handle_midi(&s, off60.data[0], off60.data[1], off60.data[2]);

  struct voice *stolen = find_voice_playing(&s, 60);
  TEST_ASSERT_NOT_NULL(stolen);
  TEST_ASSERT_TRUE(sustain_holds(stolen->note));

  /* Pedal up before the new note, so it's a plain, unsustained key press -
     isolates the steal-window race from pedal deferral. */
  struct midi_msg pedal_up =
      make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_HOLDPEDAL, 0);
  synth_handle_midi(&s, pedal_up.data[0], pedal_up.data[1], pedal_up.data[2]);

  /* Steal that voice for a brand new note */
  struct midi_msg steal_on = make_note_on(67, 64);
  synth_handle_midi(&s, steal_on.data[0], steal_on.data[1], steal_on.data[2]);
  TEST_ASSERT_EQUAL(VOICE_STEALING, stolen->state);

  /* The new note's own key is released a moment later - well inside the
     steal's RTZ fade, before find_voice_by_note() would ever match it
     (it only matches VOICE_ACTIVE - see synth_note_off()). */
  struct midi_msg steal_off = make_note_off(67);
  synth_handle_midi(&s, steal_off.data[0], steal_off.data[1],
                    steal_off.data[2]);

  /* Generous render budget - well past both the ~10ms RTZ fade and a
     full envelope release at default patch settings. Without the fix,
     the note-off above is lost and the voice sits in ENV_SUSTAIN
     indefinitely - no amount of rendering brings it back to IDLE. */
  render_n(&s, 300);

  /* The note-off that arrived mid-steal must still take effect once the
     steal resolves - the voice should have gone fully silent, not be
     stuck sounding forever with no key down and nothing left to
     deliver its release. */
  TEST_ASSERT_EQUAL(VOICE_IDLE, stolen->state);
}

/* -----------------------------------------------------------------------
 * Voice ageing
 * ----------------------------------------------------------------------- */

void test_a_voice_that_just_finished_stealing_is_not_immediately_re_stolen(
    void) {
  /*
   * Without resetting age on allocation, a voice that just finished a
   * steal still carries the highest age in the pool (it was bumped by
   * age_voices() the moment it was picked) and gets immediately
   * re-stolen for the very next note - even though its note has barely
   * started. In a fast passage with all voices busy, this pins the
   * thrashing to a single voice while the others hold their notes
   * indefinitely - "running out of notes" and a trill-like retrigger.
   */
  /* Sized for the largest SYNTH_MAX_VOICES across configs (8 in Release,
     6 in Debug/test) - only the first SYNTH_MAX_VOICES entries are used,
     so this stays in bounds either way. */
  uint8_t notes[] = {60, 62, 64, 65, 66, 68, 70, 72};
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    struct midi_msg m = make_note_on(notes[i], 64);
    synth_handle_midi(&s, m.data[0], m.data[1], m.data[2]);
    render_n(&s, 1);
  }

  /* 5th note steals the oldest (60's voice), swaps in note 67, and lets
     the steal fully settle back to ACTIVE. */
  struct midi_msg steal1 = make_note_on(67, 64);
  synth_handle_midi(&s, steal1.data[0], steal1.data[1], steal1.data[2]);
  render_n(&s, 20); /* RTZ -> OFF -> new note ACTIVE */

  int stolen_idx = -1;
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    if (s.voice[i].state == VOICE_ACTIVE && s.voice[i].note == 67) {
      stolen_idx = i;
    }
  }
  TEST_ASSERT_TRUE(stolen_idx >= 0);

  /* A 6th note now needs to steal again. The voice that just finished
     playing 67 should be the YOUNGEST in the pool, not the oldest. */
  struct midi_msg steal2 = make_note_on(69, 64);
  synth_handle_midi(&s, steal2.data[0], steal2.data[1], steal2.data[2]);

  TEST_ASSERT_NOT_EQUAL(VOICE_STEALING, s.voice[stolen_idx].state);
}

void test_age_increments_on_new_allocation(void) {
  /*
   * Each new allocation is stamped with the next trigger sequence number,
   * so an earlier note is "older" (smaller number) than a later one.
   */
  struct midi_msg m1 = make_note_on(60, 64);
  struct midi_msg m2 = make_note_on(62, 64);

  synth_handle_midi(&s, m1.data[0], m1.data[1], m1.data[2]);
  synth_handle_midi(&s, m2.data[0], m2.data[1], m2.data[2]);

  uint32_t seq60 = s.trigger_seq[find_voice_playing(&s, 60)->idx];
  uint32_t seq62 = s.trigger_seq[find_voice_playing(&s, 62)->idx];

  TEST_ASSERT_TRUE(seq60 < seq62);
}

void test_age_not_incremented_on_retrigger(void) {
  /*
   * Retrigger the same note: the find_voice_by_note path fires,
   * age_voice() is NOT called.
   */
  struct midi_msg m = make_note_on(60, 64);
  synth_handle_midi(&s, m.data[0], m.data[1], m.data[2]);
  render_n(&s, 1);
  uint32_t seq_before = s.trigger_seq[find_voice_playing(&s, 60)->idx];

  synth_handle_midi(&s, m.data[0], m.data[1],
                    m.data[2]); /* retrigger - not a new allocation */
  uint32_t seq_after = s.trigger_seq[find_voice_playing(&s, 60)->idx];

  TEST_ASSERT_EQUAL_UINT32(seq_before, seq_after);
}

/* -----------------------------------------------------------------------
 * Note-off routing
 * ----------------------------------------------------------------------- */

void test_note_off_sets_release_on_matching_voice(void) {
  struct midi_msg on = make_note_on(60, 64);
  struct midi_msg off = make_note_off(60);

  synth_handle_midi(&s, on.data[0], on.data[1], on.data[2]);
  render_n(&s, 1);
  synth_handle_midi(&s, off.data[0], off.data[1], off.data[2]);

  /* voice_gate_off() applies synchronously - envelope moves to RELEASE
     immediately, no render() needed */
  struct voice *v = find_voice_playing(&s, 60);
  TEST_ASSERT_NOT_NULL(v);
  TEST_ASSERT_EQUAL(ENV_RELEASE, v->amp_env.state);
}

void test_note_off_for_unknown_note_is_harmless(void) {
  /* No voice playing note 72 — should not crash or corrupt state */
  struct midi_msg off = make_note_off(72);
  synth_handle_midi(&s, off.data[0], off.data[1], off.data[2]);
  TEST_ASSERT_EQUAL_INT(SYNTH_MAX_VOICES, count_voice_in_state(&s, VOICE_IDLE));
}

void test_note_off_does_not_affect_other_voice(void) {
  struct midi_msg on60 = make_note_on(60, 64);
  struct midi_msg on64 = make_note_on(64, 64);
  struct midi_msg off60 = make_note_off(60);

  synth_handle_midi(&s, on60.data[0], on60.data[1], on60.data[2]);
  synth_handle_midi(&s, on64.data[0], on64.data[1], on64.data[2]);
  render_n(&s, 1);

  synth_handle_midi(&s, off60.data[0], off60.data[1], off60.data[2]);

  /* Note 64 should be unaffected — still ACTIVE, envelope not released */
  struct voice *v64 = find_voice_playing(&s, 64);
  TEST_ASSERT_NOT_NULL(v64);
  TEST_ASSERT_NOT_EQUAL(ENV_RELEASE, v64->amp_env.state);
}

/* -----------------------------------------------------------------------
 * Note-on with velocity 0 == note-off (MIDI spec)
 * ----------------------------------------------------------------------- */

void test_note_on_velocity_zero_acts_as_note_off(void) {
  struct midi_msg on = make_note_on(60, 64);
  struct midi_msg off = make_note_on(60, 0); /* velocity=0 → note-off */

  synth_handle_midi(&s, on.data[0], on.data[1], on.data[2]);
  render_n(&s, 1);
  synth_handle_midi(&s, off.data[0], off.data[1], off.data[2]);

  struct voice *v = find_voice_playing(&s, 60);
  TEST_ASSERT_NOT_NULL(v);
  TEST_ASSERT_EQUAL(ENV_RELEASE, v->amp_env.state); /* released immediately */
}

/* -----------------------------------------------------------------------
 * MIDI CC → param update
 * ----------------------------------------------------------------------- */

void test_cc_message_does_not_crash(void) {
  /* Smoke: a CC should be dispatched and not crash */
  struct midi_msg cc =
      make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_VOLUME, 100);
  synth_handle_midi(&s, cc.data[0], cc.data[1], cc.data[2]);
  /* No assertion needed — if we get here without a fault, it passed */
  TEST_PASS();
}

/* -----------------------------------------------------------------------
 * MIDI channel masking
 *
 * synth_init(&s, MIDI_OMNI) in setUp() means the parser-level channel
 * filter is bypassed, so the raw status byte (channel nibble included)
 * reaches synth_handle_midi() unfiltered - it must mask that nibble off
 * itself before dispatch, or messages on any channel but 1 go unhandled.
 * ----------------------------------------------------------------------- */

void test_note_on_is_handled_on_non_first_channel_in_omni_mode(void) {
  /* 0x91 = NOTE_ON, channel 2 */
  synth_handle_midi(&s, MIDI_STATUS_NOTE_ON | 0x01, 60, 64);
  TEST_ASSERT_EQUAL_INT(1, count_voice_in_state(&s, VOICE_ACTIVE));
}

void test_control_change_is_handled_on_non_first_channel_in_omni_mode(void) {
  /* 0xB3 = CONTROL_CHANGE, channel 4 */
  synth_handle_midi(&s, MIDI_STATUS_CONTROL_CHANGE | 0x03, MIDI_CC_VOLUME, 127);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, s.params[DCA_LEVEL].f);
}

/* -----------------------------------------------------------------------
 * Pitch bend
 * ----------------------------------------------------------------------- */

void test_pitch_bend_center_is_near_zero_semitones(void) {
  struct midi_msg bend =
      make_msg(MIDI_STATUS_PITCH_BEND, 0, 64); /* 14-bit center = 8192 */
  synth_handle_midi(&s, bend.data[0], bend.data[1], bend.data[2]);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, s.pitch_bend_semitones);
}

void test_pitch_bend_full_down_reaches_negative_bend_range(void) {
  struct midi_msg bend =
      make_msg(MIDI_STATUS_PITCH_BEND, 0, 0); /* 14-bit value = 0 */
  synth_handle_midi(&s, bend.data[0], bend.data[1], bend.data[2]);
  /* Default patch bend range is +/- 2 semitones */
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -2.0f, s.pitch_bend_semitones);
}

void test_pitch_bend_full_up_approaches_positive_bend_range(void) {
  struct midi_msg bend =
      make_msg(MIDI_STATUS_PITCH_BEND, 127, 127); /* 14-bit value = 16383 */
  synth_handle_midi(&s, bend.data[0], bend.data[1], bend.data[2]);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.0f, s.pitch_bend_semitones);
}

void test_pitch_bend_propagates_to_active_voice(void) {
  struct midi_msg on = make_note_on(60, 64);
  synth_handle_midi(&s, on.data[0], on.data[1], on.data[2]);

  struct midi_msg bend = make_msg(MIDI_STATUS_PITCH_BEND, 0, 0);
  synth_handle_midi(&s, bend.data[0], bend.data[1], bend.data[2]);

  struct voice *v = find_voice_playing(&s, 60);
  TEST_ASSERT_NOT_NULL(v);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -2.0f, v->pitch_bend_semitones);
}

/* -----------------------------------------------------------------------
 * Mod wheel
 * ----------------------------------------------------------------------- */

void test_mod_wheel_cc_sets_normalized_value(void) {
  struct midi_msg wheel =
      make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_MODULATIONWHEEL, 127);
  synth_handle_midi(&s, wheel.data[0], wheel.data[1], wheel.data[2]);

  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f, s.mod_wheel);
}

void test_mod_wheel_zero_cc_gives_zero(void) {
  struct midi_msg wheel =
      make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_MODULATIONWHEEL, 0);
  synth_handle_midi(&s, wheel.data[0], wheel.data[1], wheel.data[2]);

  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, s.mod_wheel);
}

void test_mod_wheel_propagates_to_active_voice(void) {
  struct midi_msg on = make_note_on(60, 64);
  synth_handle_midi(&s, on.data[0], on.data[1], on.data[2]);

  struct midi_msg wheel =
      make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_MODULATIONWHEEL, 127);
  synth_handle_midi(&s, wheel.data[0], wheel.data[1], wheel.data[2]);

  struct voice *v = find_voice_playing(&s, 60);
  TEST_ASSERT_NOT_NULL(v);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f, v->mod_wheel);
}

void test_mod_wheel_dest_defaults_to_vco(void) {
  TEST_ASSERT_EQUAL(MOD_WHEEL_DEST_VCO, s.params[SYNTH_MOD_WHEEL_DEST].e);
}

/* -----------------------------------------------------------------------
 * Program change
 * ----------------------------------------------------------------------- */

void test_program_change_loads_resolved_patch(void) {
  struct midi_msg pc = make_msg(MIDI_STATUS_PROGRAM_CHANGE, FAKE_LOADER_SLOT, 0);
  synth_handle_midi(&s, pc.data[0], pc.data[1], pc.data[2]);

  TEST_ASSERT_EQUAL(VOICE_MODE_MONO, s.params[SYNTH_VOICE_MODE].e);
  TEST_ASSERT_TRUE(s.params[SYNTH_PORTAMENTO_ON].b);
}

void test_program_change_falls_back_to_default_patch_when_unresolved(void) {
  /* Land on the fake loader's patch first so the synth is demonstrably
     not already at its defaults. */
  struct midi_msg loaded =
      make_msg(MIDI_STATUS_PROGRAM_CHANGE, FAKE_LOADER_SLOT, 0);
  synth_handle_midi(&s, loaded.data[0], loaded.data[1], loaded.data[2]);
  TEST_ASSERT_EQUAL(VOICE_MODE_MONO, s.params[SYNTH_VOICE_MODE].e);

  /* A slot the fake loader doesn't resolve must fall back to the default
     patch, not leave the mono patch in place. */
  struct midi_msg unresolved =
      make_msg(MIDI_STATUS_PROGRAM_CHANGE, FAKE_LOADER_SLOT + 1, 0);
  synth_handle_midi(&s, unresolved.data[0], unresolved.data[1],
                    unresolved.data[2]);

  TEST_ASSERT_EQUAL(VOICE_MODE_POLY, s.params[SYNTH_VOICE_MODE].e);
  TEST_ASSERT_FALSE(s.params[SYNTH_PORTAMENTO_ON].b);
}

void test_program_change_with_no_loader_wired_falls_back_to_default(void) {
  synth_init(&s, MIDI_OMNI, NULL); /* re-init with no loader at all */

  struct midi_msg pc = make_msg(MIDI_STATUS_PROGRAM_CHANGE, FAKE_LOADER_SLOT, 0);
  synth_handle_midi(&s, pc.data[0], pc.data[1], pc.data[2]);

  TEST_ASSERT_EQUAL(VOICE_MODE_POLY, s.params[SYNTH_VOICE_MODE].e);
}

/*
 * A patch load that also changes voice mode resets every voice (to avoid
 * stuck notes).  That reset must not discard the chain update the load has
 * just requested, otherwise the voices keep running on the previous patch.
 * The checks are on what the voices actually applied (amp envelope sustain),
 * not just on s.params.
 */
static void assert_all_voice_sustain(float expected) {
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, expected, s.voice[i].amp_env.sustain);
  }
}

void test_load_patch_poly_to_mono_applies_voice_params(void) {
  render_n(&s, 1); /* apply the init patch */

  param_value_t patch[PARAM_COUNT];
  param_create_default_patch(patch);
  patch[SYNTH_VOICE_MODE].e = VOICE_MODE_MONO;
  patch[DCA_SUSTAIN].f = 0.5f;

  synth_load_patch(&s, patch);
  render_n(&s, 1);

  TEST_ASSERT_EQUAL(VOICE_MODE_MONO, s.mode);
  assert_all_voice_sustain(0.5f);
}

void test_load_patch_mono_to_poly_applies_voice_params(void) {
  param_value_t patch[PARAM_COUNT];
  param_create_default_patch(patch);
  patch[SYNTH_VOICE_MODE].e = VOICE_MODE_MONO;
  synth_load_patch(&s, patch);
  render_n(&s, 1);

  param_create_default_patch(patch);
  patch[DCA_SUSTAIN].f = 0.25f; /* default mode is POLY */
  synth_load_patch(&s, patch);
  render_n(&s, 1);

  TEST_ASSERT_EQUAL(VOICE_MODE_POLY, s.mode);
  assert_all_voice_sustain(0.25f);
}

/* -----------------------------------------------------------------------
 * Sustain pedal
 * ----------------------------------------------------------------------- */

void test_sustain_defers_poly_release_until_pedal_up(void) {
  struct midi_msg on = make_note_on(60, 64);
  struct midi_msg pedal_down =
      make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_HOLDPEDAL, 127);
  struct midi_msg off = make_note_off(60);

  synth_handle_midi(&s, on.data[0], on.data[1], on.data[2]);
  render_n(&s, 1);
  synth_handle_midi(&s, pedal_down.data[0], pedal_down.data[1],
                    pedal_down.data[2]);
  synth_handle_midi(&s, off.data[0], off.data[1], off.data[2]);

  /* Held by the pedal - voice_gate_off() must NOT have been called yet */
  struct voice *v = find_voice_playing(&s, 60);
  TEST_ASSERT_NOT_NULL(v);
  TEST_ASSERT_TRUE(sustain_holds(v->note));
  TEST_ASSERT_NOT_EQUAL(ENV_RELEASE, v->amp_env.state);

  struct midi_msg pedal_up =
      make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_HOLDPEDAL, 0);
  synth_handle_midi(&s, pedal_up.data[0], pedal_up.data[1], pedal_up.data[2]);

  TEST_ASSERT_FALSE(sustain_holds(v->note));
  TEST_ASSERT_EQUAL(ENV_RELEASE, v->amp_env.state);
}

static void send(struct midi_msg m) {
  synth_handle_midi(&s, m.data[0], m.data[1], m.data[2]);
}

/*
 * Re-pressing a key that the pedal is holding makes it a held key again, so
 * lifting the pedal must not release it.
 */
void test_sustain_repressed_note_survives_pedal_up(void) {
  send(make_note_on(60, 64));
  render_n(&s, 1);
  send(make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_HOLDPEDAL, 127));
  send(make_note_off(60)); /* now held by the pedal */
  send(make_note_on(60, 64)); /* key pressed again - retrigger */

  struct voice *v = find_voice_playing(&s, 60);
  TEST_ASSERT_NOT_NULL(v);
  TEST_ASSERT_FALSE(sustain_holds(v->note));

  send(make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_HOLDPEDAL, 0));

  TEST_ASSERT_NOT_EQUAL(ENV_RELEASE, v->amp_env.state);
}

/* ...and releasing it again while the pedal is down is held as normal */
void test_sustain_repressed_note_released_again_is_held_until_pedal_up(void) {
  send(make_note_on(60, 64));
  render_n(&s, 1);
  send(make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_HOLDPEDAL, 127));
  send(make_note_off(60));
  send(make_note_on(60, 64));
  render_n(&s, 1);
  send(make_note_off(60));

  struct voice *v = find_voice_playing(&s, 60);
  TEST_ASSERT_NOT_NULL(v);
  TEST_ASSERT_TRUE(sustain_holds(v->note));
  TEST_ASSERT_NOT_EQUAL(ENV_RELEASE, v->amp_env.state);

  send(make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_HOLDPEDAL, 0));

  TEST_ASSERT_EQUAL(ENV_RELEASE, v->amp_env.state);
}

void test_sustain_does_not_affect_notes_without_pedal(void) {
  struct midi_msg on = make_note_on(60, 64);
  struct midi_msg off = make_note_off(60);

  synth_handle_midi(&s, on.data[0], on.data[1], on.data[2]);
  render_n(&s, 1);
  synth_handle_midi(&s, off.data[0], off.data[1], off.data[2]);

  struct voice *v = find_voice_playing(&s, 60);
  TEST_ASSERT_NOT_NULL(v);
  TEST_ASSERT_EQUAL(ENV_RELEASE, v->amp_env.state);
}

void test_sustain_defers_mono_release_until_pedal_up(void) {
  struct midi_msg poly_off =
      make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_MONOMODEON, 0);
  struct midi_msg pedal_down =
      make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_HOLDPEDAL, 127);
  struct midi_msg on = make_note_on(60, 64);
  struct midi_msg off = make_note_off(60);

  synth_handle_midi(&s, poly_off.data[0], poly_off.data[1],
                    poly_off.data[2]); /* -> MONO */
  synth_handle_midi(&s, pedal_down.data[0], pedal_down.data[1],
                    pedal_down.data[2]);
  synth_handle_midi(&s, on.data[0], on.data[1], on.data[2]);
  render_n(&s, 1);
  synth_handle_midi(&s, off.data[0], off.data[1], off.data[2]);

  /* Still held by the pedal - all voices remain ACTIVE */
  TEST_ASSERT_EQUAL_INT(SYNTH_MAX_VOICES,
                        count_voice_in_state(&s, VOICE_ACTIVE));

  struct midi_msg pedal_up =
      make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_HOLDPEDAL, 0);
  synth_handle_midi(&s, pedal_up.data[0], pedal_up.data[1], pedal_up.data[2]);

  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    TEST_ASSERT_EQUAL(ENV_RELEASE, s.voice[i].amp_env.state);
  }
}

/* -----------------------------------------------------------------------
 * Sostenuto pedal
 * ----------------------------------------------------------------------- */

void test_sostenuto_defers_release_only_for_notes_sounding_at_press(void) {
  struct midi_msg on60 = make_note_on(60, 64);
  struct midi_msg pedal_down =
      make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_SOSTENUTO, 127);
  struct midi_msg on64 = make_note_on(64, 64);
  struct midi_msg off60 = make_note_off(60);
  struct midi_msg off64 = make_note_off(64);

  /* 60 is already sounding when the pedal goes down - 64 is played after */
  synth_handle_midi(&s, on60.data[0], on60.data[1], on60.data[2]);
  render_n(&s, 1);
  synth_handle_midi(&s, pedal_down.data[0], pedal_down.data[1],
                    pedal_down.data[2]);
  synth_handle_midi(&s, on64.data[0], on64.data[1], on64.data[2]);
  render_n(&s, 1);

  synth_handle_midi(&s, off60.data[0], off60.data[1], off60.data[2]);
  synth_handle_midi(&s, off64.data[0], off64.data[1], off64.data[2]);

  struct voice *v60 = find_voice_playing(&s, 60);
  struct voice *v64 = find_voice_playing(&s, 64);
  TEST_ASSERT_NOT_NULL(v60);
  TEST_ASSERT_NOT_NULL(v64);

  /* 60 was captured - held past its note-off */
  TEST_ASSERT_TRUE(sostenuto_holds(v60->note));
  TEST_ASSERT_NOT_EQUAL(ENV_RELEASE, v60->amp_env.state);

  /* 64 started after the pedal went down - releases immediately */
  TEST_ASSERT_FALSE(sostenuto_holds(v64->note));
  TEST_ASSERT_EQUAL(ENV_RELEASE, v64->amp_env.state);

  struct midi_msg pedal_up =
      make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_SOSTENUTO, 0);
  synth_handle_midi(&s, pedal_up.data[0], pedal_up.data[1], pedal_up.data[2]);

  TEST_ASSERT_FALSE(sostenuto_holds(v60->note));
  TEST_ASSERT_EQUAL(ENV_RELEASE, v60->amp_env.state);
}

/*
 * Same as the sustain case: a captured note that is re-pressed is a held key
 * again, so lifting the sostenuto pedal must not release it.
 */
void test_sostenuto_repressed_note_survives_pedal_up(void) {
  send(make_note_on(60, 64));
  render_n(&s, 1);
  send(make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_SOSTENUTO, 127));
  send(make_note_off(60)); /* captured, held by sostenuto */
  send(make_note_on(60, 64)); /* key pressed again - retrigger */

  struct voice *v = find_voice_playing(&s, 60);
  TEST_ASSERT_NOT_NULL(v);
  TEST_ASSERT_FALSE(sostenuto_holds(v->note));

  send(make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_SOSTENUTO, 0));

  TEST_ASSERT_NOT_EQUAL(ENV_RELEASE, v->amp_env.state);
}

/*
 * The retrigger keeps the sostenuto capture - the pedal went down while this
 * note was sounding and is still down - so releasing the key again is held.
 */
void test_sostenuto_repressed_note_released_again_is_still_held(void) {
  send(make_note_on(60, 64));
  render_n(&s, 1);
  send(make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_SOSTENUTO, 127));
  send(make_note_off(60));
  send(make_note_on(60, 64));
  render_n(&s, 1);
  send(make_note_off(60));

  struct voice *v = find_voice_playing(&s, 60);
  TEST_ASSERT_NOT_NULL(v);
  TEST_ASSERT_TRUE(sostenuto_holds(v->note));
  TEST_ASSERT_NOT_EQUAL(ENV_RELEASE, v->amp_env.state);

  send(make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_SOSTENUTO, 0));

  TEST_ASSERT_EQUAL(ENV_RELEASE, v->amp_env.state);
}

void test_sostenuto_does_not_affect_notes_without_pedal(void) {
  struct midi_msg on = make_note_on(60, 64);
  struct midi_msg off = make_note_off(60);

  synth_handle_midi(&s, on.data[0], on.data[1], on.data[2]);
  render_n(&s, 1);
  synth_handle_midi(&s, off.data[0], off.data[1], off.data[2]);

  struct voice *v = find_voice_playing(&s, 60);
  TEST_ASSERT_NOT_NULL(v);
  TEST_ASSERT_EQUAL(ENV_RELEASE, v->amp_env.state);
}

void test_sostenuto_and_sustain_both_held_require_both_released(void) {
  struct midi_msg on = make_note_on(60, 64);
  struct midi_msg sustain_down =
      make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_HOLDPEDAL, 127);
  struct midi_msg sostenuto_down =
      make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_SOSTENUTO, 127);
  struct midi_msg off = make_note_off(60);

  synth_handle_midi(&s, on.data[0], on.data[1], on.data[2]);
  render_n(&s, 1);
  synth_handle_midi(&s, sustain_down.data[0], sustain_down.data[1],
                    sustain_down.data[2]);
  synth_handle_midi(&s, sostenuto_down.data[0], sostenuto_down.data[1],
                    sostenuto_down.data[2]);
  synth_handle_midi(&s, off.data[0], off.data[1], off.data[2]);

  struct voice *v = find_voice_playing(&s, 60);
  TEST_ASSERT_NOT_NULL(v);
  TEST_ASSERT_TRUE(sustain_holds(v->note));
  TEST_ASSERT_TRUE(sostenuto_holds(v->note));

  /* Lifting sustain alone must not release it - sostenuto still holds it */
  struct midi_msg sustain_up =
      make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_HOLDPEDAL, 0);
  synth_handle_midi(&s, sustain_up.data[0], sustain_up.data[1],
                    sustain_up.data[2]);

  TEST_ASSERT_FALSE(sustain_holds(v->note));
  TEST_ASSERT_NOT_EQUAL(ENV_RELEASE, v->amp_env.state);

  /* Lifting sostenuto too finally releases it */
  struct midi_msg sostenuto_up =
      make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_SOSTENUTO, 0);
  synth_handle_midi(&s, sostenuto_up.data[0], sostenuto_up.data[1],
                    sostenuto_up.data[2]);

  TEST_ASSERT_FALSE(sostenuto_holds(v->note));
  TEST_ASSERT_EQUAL(ENV_RELEASE, v->amp_env.state);
}

void test_sostenuto_defers_mono_release_only_for_note_sounding_at_press(void) {
  struct midi_msg mono_on =
      make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_MONOMODEON, 0);
  struct midi_msg on60 = make_note_on(60, 64);
  struct midi_msg pedal_down =
      make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_SOSTENUTO, 127);
  struct midi_msg off60 = make_note_off(60);

  synth_handle_midi(&s, mono_on.data[0], mono_on.data[1],
                    mono_on.data[2]); /* -> MONO */
  synth_handle_midi(&s, on60.data[0], on60.data[1], on60.data[2]);
  render_n(&s, 1);
  synth_handle_midi(&s, pedal_down.data[0], pedal_down.data[1],
                    pedal_down.data[2]);
  synth_handle_midi(&s, off60.data[0], off60.data[1], off60.data[2]);

  /* 60 was already sounding before the pedal went down - still held */
  TEST_ASSERT_EQUAL_INT(SYNTH_MAX_VOICES,
                        count_voice_in_state(&s, VOICE_ACTIVE));

  struct midi_msg pedal_up =
      make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_SOSTENUTO, 0);
  synth_handle_midi(&s, pedal_up.data[0], pedal_up.data[1], pedal_up.data[2]);

  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    TEST_ASSERT_EQUAL(ENV_RELEASE, s.voice[i].amp_env.state);
  }
}

void test_sostenuto_does_not_hold_a_mono_note_started_after_press(void) {
  struct midi_msg mono_on =
      make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_MONOMODEON, 0);
  struct midi_msg pedal_down =
      make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_SOSTENUTO, 127);
  struct midi_msg on60 = make_note_on(60, 64);
  struct midi_msg off60 = make_note_off(60);

  synth_handle_midi(&s, mono_on.data[0], mono_on.data[1],
                    mono_on.data[2]); /* -> MONO */
  /* Pedal pressed while nothing is sounding yet */
  synth_handle_midi(&s, pedal_down.data[0], pedal_down.data[1],
                    pedal_down.data[2]);
  synth_handle_midi(&s, on60.data[0], on60.data[1], on60.data[2]);
  render_n(&s, 1);
  synth_handle_midi(&s, off60.data[0], off60.data[1], off60.data[2]);

  /* 60 started after the pedal went down - releases immediately */
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    TEST_ASSERT_EQUAL(ENV_RELEASE, s.voice[i].amp_env.state);
  }
}

/* -----------------------------------------------------------------------
 * Unison detune (mono mode)
 * ----------------------------------------------------------------------- */

void test_unison_detune_spreads_mono_voices_around_the_note(void) {
  struct midi_msg mono =
      make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_MONOMODEON, 0);
  struct midi_msg detune =
      make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_DETUNELEVEL, 127);

  synth_handle_midi(&s, mono.data[0], mono.data[1], mono.data[2]);
  synth_handle_midi(&s, detune.data[0], detune.data[1], detune.data[2]);

  /* Spread across the voices, centred on the note: zero mean, +/-50c max */
  float sum = 0.0f, widest = 0.0f;
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    sum += s.voice[i].unison_cents;
    widest = fmaxf(widest, fabsf(s.voice[i].unison_cents));
  }
  TEST_ASSERT_TRUE(s.voice[0].unison_cents < 0.0f);
  TEST_ASSERT_TRUE(s.voice[SYNTH_MAX_VOICES - 1].unison_cents > 0.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, sum);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, widest);
}

void test_unison_detune_stays_zero_in_poly_mode(void) {
  /* Default mode is POLY - a detune CC must not detune independent voices */
  struct midi_msg detune =
      make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_DETUNELEVEL, 127);
  synth_handle_midi(&s, detune.data[0], detune.data[1], detune.data[2]);

  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, s.voice[i].unison_cents);
  }
}

/* -----------------------------------------------------------------------
 * Channel mode messages, CC120-127 and System Reset (design decision D2)
 * ----------------------------------------------------------------------- */

static void cc(uint8_t number, uint8_t value) {
  synth_handle_midi(&s, MIDI_STATUS_CONTROL_CHANGE, number, value);
}

static void play(uint8_t note) {
  synth_handle_midi(&s, MIDI_STATUS_NOTE_ON, note, 100);
}

/* A patch with a long release, so a released voice stays ACTIVE a while */
static void load_long_release(enum voice_mode mode) {
  param_value_t p[PARAM_COUNT];
  param_create_default_patch(p);
  p[SYNTH_VOICE_MODE].e = mode;
  p[DCA_RELEASE].f = 0.5f;
  synth_load_patch(&s, p);
}

/* CC123 lifts every key as if by hand: notes release, they aren't cut */
void test_cc123_releases_held_keys(void) {
  load_long_release(VOICE_MODE_POLY);
  play(60);
  play(64);
  render_n(&s, 2);

  cc(MIDI_CC_ALLNOTESOFF, 0);

  TEST_ASSERT_FALSE(keys_any_down(&s.keys));
  TEST_ASSERT_EQUAL_INT(2, count_voice_in_state(&s, VOICE_ACTIVE));
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    TEST_ASSERT_FALSE(s.voice[i].gate);
  }
}

/* ...and the pedals still apply: a sustained note keeps sounding */
void test_cc123_respects_the_sustain_pedal(void) {
  cc(MIDI_CC_HOLDPEDAL, 127);
  play(60);
  render_n(&s, 1);

  cc(MIDI_CC_ALLNOTESOFF, 0);

  struct voice *v = find_voice_playing(&s, 60);
  TEST_ASSERT_NOT_NULL(v);
  TEST_ASSERT_TRUE(v->gate);
  TEST_ASSERT_TRUE(sustain_holds(60));

  cc(MIDI_CC_HOLDPEDAL, 0);
  TEST_ASSERT_FALSE(v->gate);
}

/* CC120 is the hard kill - immediate silence, even with pedals down */
void test_cc120_silences_everything_at_once(void) {
  load_long_release(VOICE_MODE_POLY);
  cc(MIDI_CC_HOLDPEDAL, 127);
  cc(MIDI_CC_SOSTENUTO, 127);
  play(60);
  play(64);
  render_n(&s, 1);

  cc(MIDI_CC_ALLSOUNDOFF, 0);

  TEST_ASSERT_EQUAL_INT(SYNTH_MAX_VOICES,
                        count_voice_in_state(&s, VOICE_IDLE));
  TEST_ASSERT_FALSE(keys_any_gated(&s.keys));

  /* the next note plays normally */
  play(67);
  TEST_ASSERT_NOT_NULL(find_voice_playing(&s, 67));
}

/* CC121 lifts both pedals and centres bend and wheel; keys down sound on */
void test_cc121_lifts_pedals_and_centres_controllers(void) {
  load_long_release(VOICE_MODE_POLY);
  cc(MIDI_CC_HOLDPEDAL, 127);
  play(60);
  synth_handle_midi(&s, MIDI_STATUS_NOTE_OFF, 60, 0); /* sustained */
  play(64);                                            /* key down */
  synth_handle_midi(&s, MIDI_STATUS_PITCH_BEND, 0, 127);
  cc(MIDI_CC_MODULATIONWHEEL, 127);
  render_n(&s, 1);

  cc(MIDI_CC_RESETALLCONTROLLERS, 0);

  TEST_ASSERT_FALSE(s.keys.sustain_pedal);
  TEST_ASSERT_FALSE(find_voice_playing(&s, 60)->gate); /* released */
  TEST_ASSERT_TRUE(find_voice_playing(&s, 64)->gate);  /* still held */
  TEST_ASSERT_EQUAL_FLOAT(0.0f, s.pitch_bend_semitones);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, s.mod_wheel);
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, s.voice[i].pitch_bend_semitones);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, s.voice[i].mod_wheel);
  }
}

/* CC126 Mono On / CC127 Poly On set the voice mode (and act as CC123) */
void test_cc126_and_cc127_select_the_voice_mode(void) {
  play(60);
  cc(MIDI_CC_MONOMODEON, 0);
  TEST_ASSERT_EQUAL(VOICE_MODE_MONO, s.params[SYNTH_VOICE_MODE].e);
  TEST_ASSERT_EQUAL_PTR(&mono_assigner, s.assigner);
  TEST_ASSERT_FALSE(keys_any_down(&s.keys));

  play(62);
  cc(MIDI_CC_POLYMODEON, 0);
  TEST_ASSERT_EQUAL(VOICE_MODE_POLY, s.params[SYNTH_VOICE_MODE].e);
  TEST_ASSERT_EQUAL_PTR(&poly_assigner, s.assigner);
  TEST_ASSERT_FALSE(keys_any_down(&s.keys));
}

/* Omni Off/On aren't supported but still act as All Notes Off */
void test_cc124_and_cc125_act_as_all_notes_off(void) {
  play(60);
  cc(MIDI_CC_OMNIMODEOFF, 0);
  TEST_ASSERT_FALSE(keys_any_down(&s.keys));

  play(62);
  cc(MIDI_CC_OMNIMODEON, 0);
  TEST_ASSERT_FALSE(keys_any_down(&s.keys));
  TEST_ASSERT_EQUAL(VOICE_MODE_POLY, s.params[SYNTH_VOICE_MODE].e);
}

/* Channel mode CCs never reach the parameter map (CC127 used to) */
void test_cc127_no_longer_changes_a_parameter_by_value(void) {
  cc(MIDI_CC_MONOMODEON, 0);
  cc(MIDI_CC_POLYMODEON, 127); /* any value means Poly On */
  TEST_ASSERT_EQUAL(VOICE_MODE_POLY, s.params[SYNTH_VOICE_MODE].e);
  cc(MIDI_CC_POLYMODEON, 0);
  TEST_ASSERT_EQUAL(VOICE_MODE_POLY, s.params[SYNTH_VOICE_MODE].e);
}

/* System Reset (0xFF): silence, every key and pedal forgotten, controllers
 * at rest; the patch is untouched */
void test_system_reset_returns_to_rest(void) {
  load_long_release(VOICE_MODE_MONO);
  cc(MIDI_CC_HOLDPEDAL, 127);
  play(60);
  synth_handle_midi(&s, MIDI_STATUS_PITCH_BEND, 0, 0);
  cc(MIDI_CC_MODULATIONWHEEL, 100);
  render_n(&s, 1);

  synth_handle_midi(&s, MIDI_STATUS_SYS_RESET, 0, 0);

  TEST_ASSERT_EQUAL_INT(SYNTH_MAX_VOICES,
                        count_voice_in_state(&s, VOICE_IDLE));
  TEST_ASSERT_FALSE(keys_any_gated(&s.keys));
  TEST_ASSERT_FALSE(s.keys.sustain_pedal);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, s.pitch_bend_semitones);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, s.mod_wheel);
  TEST_ASSERT_EQUAL(VOICE_MODE_MONO, s.params[SYNTH_VOICE_MODE].e);
  TEST_ASSERT_EQUAL_FLOAT(0.5f, s.params[DCA_RELEASE].f);
}

/* -----------------------------------------------------------------------
 * Portamento (mono-only glide)
 * ----------------------------------------------------------------------- */

void test_portamento_first_mono_note_does_not_glide(void) {
  synth_handle_midi(&s, MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_MONOMODEON, 0);
  synth_handle_midi(&s, MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_PORTAMENTO, 127);
  synth_handle_midi(&s, MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_PORTAMENTOTIME,
                    127); /* long glide */

  struct midi_msg on = make_note_on(60, 64);
  synth_handle_midi(&s, on.data[0], on.data[1], on.data[2]);
  render_n(&s, 1);

  /* The first note of a phrase snaps - only legato transitions glide */
  TEST_ASSERT_EQUAL_FLOAT(s.voice[0].pitch, s.voice[0].glide_pitch);
}

void test_portamento_legato_glides_toward_target_over_several_blocks(void) {
  synth_handle_midi(&s, MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_MONOMODEON, 0);
  synth_handle_midi(&s, MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_PORTAMENTO, 127);
  synth_handle_midi(&s, MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_PORTAMENTOTIME,
                    127); /* slow glide */

  struct midi_msg on = make_note_on(60, 64);
  synth_handle_midi(&s, on.data[0], on.data[1], on.data[2]);
  render_n(&s, 1);
  float start_pitch = s.voice[0].glide_pitch;

  struct midi_msg on2 =
      make_note_on(72, 64); /* legato while 60 still held -> +1 octave */
  synth_handle_midi(&s, on2.data[0], on2.data[1], on2.data[2]);
  float target_pitch =
      s.voice[0].pitch; /* updated immediately by voice_set_note() */

  TEST_ASSERT_TRUE(target_pitch > start_pitch);

  render_n(&s, 1);
  float after_one_block = s.voice[0].glide_pitch;

  /* Moved toward the target but a slow glide shouldn't reach it in one block */
  TEST_ASSERT_TRUE(after_one_block > start_pitch);
  TEST_ASSERT_TRUE(after_one_block < target_pitch);
}

void test_portamento_disabled_jumps_immediately_on_legato(void) {
  synth_handle_midi(&s, MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_MONOMODEON,
                    0); /* mono; portamento off (default) */

  struct midi_msg on = make_note_on(60, 64);
  synth_handle_midi(&s, on.data[0], on.data[1], on.data[2]);
  render_n(&s, 1);

  struct midi_msg on2 = make_note_on(72, 64);
  synth_handle_midi(&s, on2.data[0], on2.data[1], on2.data[2]);
  render_n(&s, 1);

  /* With portamento off, the DCO pitch jumps to the target immediately -
     glide_pitch is left stale since the glide step never runs. */
  TEST_ASSERT_EQUAL_FLOAT(s.voice[0].pitch, s.voice[0].dco_1.pitch);
}

/* -----------------------------------------------------------------------
 * synth_render() — idle-voice gating
 *
 * synth_render() only calls voice_advance() for non-idle voice.  With all
 * voice idle, repeated ticking must leave them all idle.
 * ----------------------------------------------------------------------- */

void test_advance_idle_voice_stay_idle(void) {
  render_n(&s, 10);
  TEST_ASSERT_EQUAL_INT(SYNTH_MAX_VOICES, count_voice_in_state(&s, VOICE_IDLE));
}

void test_advance_active_voice_does_not_become_idle_without_note_off(void) {
  /*
   * Default patch: sustain=SQ15_ONE, release=0.
   * With no note-off, the envelope holds in SUSTAIN indefinitely.
   * The voice must remain ACTIVE across many ticks.
   */
  struct midi_msg on = make_note_on(60, 64);
  synth_handle_midi(&s, on.data[0], on.data[1], on.data[2]);
  render_n(&s, 50);
  TEST_ASSERT_EQUAL_INT(1, count_voice_in_state(&s, VOICE_ACTIVE));
}

/* -----------------------------------------------------------------------
 * Test runner
 * ----------------------------------------------------------------------- */

/* -----------------------------------------------------------------------
 * Voice steal edge cases (C5)
 *
 * The patch has a long release so voices are still ACTIVE after a note-off,
 * which makes the next phrase steal them - a ~10ms RTZ fade during which the
 * voices are VOICE_STEALING.  Events inside that window are the cases here.
 * ----------------------------------------------------------------------- */

/* Comfortably longer than the RTZ fade (10ms = ~4 blocks at 128/48kHz) */
#define RTZ_BLOCKS (8)

static void load_long_release_patch(enum voice_mode mode, float detune) {
  param_value_t p[PARAM_COUNT];
  param_create_default_patch(p);
  p[SYNTH_VOICE_MODE].e = mode;
  p[DCA_RELEASE].f = 0.5f; /* 5s */
  p[SYNTH_UNISON_DETUNE].f = detune;
  synth_load_patch(&s, p);
  render_n(&s, 1);
}

/* Start a mono phrase on 60 and release it, so the voices are in release */
static void mono_phrase_in_release(void) {
  send(make_note_on(60, 100));
  render_n(&s, 4);
  send(make_note_off(60));
  render_n(&s, 1);
}

static void assert_all_voices(enum voice_state state, uint8_t note) {
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    TEST_ASSERT_EQUAL(state, s.voice[i].state);
    TEST_ASSERT_EQUAL_UINT8(note, s.voice[i].note);
  }
}

static void assert_all_amp_env(enum env_state state) {
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    TEST_ASSERT_EQUAL(state, s.voice[i].amp_env.state);
  }
}

/* C5a: a legato note during the steal is the note that starts */
void test_mono_legato_during_steal_starts_the_held_note(void) {
  load_long_release_patch(VOICE_MODE_MONO, 0.0f);
  mono_phrase_in_release();

  send(make_note_on(62, 100)); /* new phrase - steal begins */
  TEST_ASSERT_EQUAL(VOICE_STEALING, s.voice[0].state);
  send(make_note_on(64, 100)); /* legato while still stealing */
  render_n(&s, RTZ_BLOCKS);

  assert_all_voices(VOICE_ACTIVE, 64);

  send(make_note_off(64)); /* back to the held 62 */
  assert_all_voices(VOICE_ACTIVE, 62);
}

/* C5b: a short note during the steal is played and released, not cut */
void test_mono_short_note_during_steal_is_played_then_released(void) {
  load_long_release_patch(VOICE_MODE_MONO, 0.0f);
  mono_phrase_in_release();

  send(make_note_on(62, 100));
  send(make_note_off(62)); /* key up before the fade finishes */

  /* Not hard-reset: still fading out */
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    TEST_ASSERT_EQUAL(VOICE_STEALING, s.voice[i].state);
    TEST_ASSERT_FALSE(s.voice[i].gate); /* release deferred to the new note */
  }

  render_n(&s, RTZ_BLOCKS);

  assert_all_voices(VOICE_ACTIVE, 62);
  assert_all_amp_env(ENV_RELEASE);
}

/* C5c: a new first note while the voices are still stealing retargets */
void test_mono_new_phrase_during_steal_retargets_the_steal(void) {
  load_long_release_patch(VOICE_MODE_MONO, 0.0f);
  send(make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_HOLDPEDAL, 127));
  mono_phrase_in_release(); /* release deferred by the pedal */

  send(make_note_on(62, 100)); /* first note - steal begins */
  send(make_note_off(62));     /* stack empty again, pedal holds */
  send(make_note_on(64, 90));  /* first note again, voices still stealing */
  render_n(&s, RTZ_BLOCKS);

  assert_all_voices(VOICE_ACTIVE, 64);
  TEST_ASSERT_EQUAL_UINT8(90, s.voice[0].velocity);
  assert_all_amp_env(ENV_SUSTAIN); /* key 64 is held */
}

/* Lifting the pedal during the steal releases the incoming note */
void test_mono_pedal_up_during_steal_does_not_leave_a_stuck_note(void) {
  load_long_release_patch(VOICE_MODE_MONO, 0.0f);
  send(make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_HOLDPEDAL, 127));
  mono_phrase_in_release();

  send(make_note_on(62, 100));
  send(make_note_off(62));
  send(make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_HOLDPEDAL, 0));
  render_n(&s, RTZ_BLOCKS);

  assert_all_voices(VOICE_ACTIVE, 62);
  assert_all_amp_env(ENV_RELEASE);
}

/* A steal keeps each voice's unison detune */
void test_mono_steal_keeps_unison_detune(void) {
  load_long_release_patch(VOICE_MODE_MONO, 0.2f);
  mono_phrase_in_release();

  send(make_note_on(62, 100));
  render_n(&s, RTZ_BLOCKS);

  assert_all_voices(VOICE_ACTIVE, 62);
  /* Detune is applied with the bend, so compare what the oscillators run at */
  TEST_ASSERT_TRUE(s.voice[0].dco_1.inc < s.voice[SYNTH_MAX_VOICES - 1].dco_1.inc);
}

/*
 * Poly: a note-off that races a steal with the pedal down marks the stealing
 * voice sustained.  Lifting the pedal before the fade ends must defer the
 * release to the incoming note, not turn the fade into the old note's full
 * release and then leave the new note stuck on.
 */
void test_poly_pedal_up_during_steal_race_does_not_stick_note(void) {
  load_long_release_patch(VOICE_MODE_POLY, 0.0f);
  for (uint8_t n = 0; n < SYNTH_MAX_VOICES; n++) {
    send(make_note_on(40 + n, 100)); /* fill every voice, keys held */
    render_n(&s, 1);
  }

  send(make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_HOLDPEDAL, 127));
  send(make_note_on(70, 100)); /* steals the oldest voice */
  send(make_note_off(70));     /* races the steal, pedal down */
  send(make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_HOLDPEDAL, 0));
  render_n(&s, RTZ_BLOCKS);

  struct voice *v = find_voice_playing(&s, 70);
  TEST_ASSERT_NOT_NULL(v);
  TEST_ASSERT_EQUAL(ENV_RELEASE, v->amp_env.state);
}

/* -----------------------------------------------------------------------
 * Chorus and soft takeover CCs (C8)
 * ----------------------------------------------------------------------- */

/*
 * C12: the chorus delay line is only written while the chorus runs, so it
 * must be cleared when the chorus is switched back on - otherwise the audio
 * from when it was last switched off is heard as a short burst.
 */
void test_chorus_delay_line_is_clear_when_switched_on(void) {
  param_value_t on = {.b = true}, off = {.b = false};

  synth_set_param(&s, SYNTH_CHORUS_ON, on);
  send(make_note_on(60, 100));
  render_n(&s, 20); /* fill the delay line with audio */
  synth_set_param(&s, SYNTH_CHORUS_ON, off);
  send(make_note_off(60));
  render_n(&s, 4);

  synth_set_param(&s, SYNTH_CHORUS_ON, on);

  for (uint32_t i = 0; i < CHORUS_BUFFER_SIZE; i++) {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, s.chorus.buffer[i]);
  }

  synth_set_param(&s, SYNTH_CHORUS_ON, off);
}

/* Re-sending "on" while already on must not clear a running chorus */
void test_chorus_delay_line_is_kept_while_chorus_stays_on(void) {
  param_value_t on = {.b = true}, off = {.b = false};

  synth_set_param(&s, SYNTH_CHORUS_ON, on);
  send(make_note_on(60, 100));
  render_n(&s, 20);
  synth_set_param(&s, SYNTH_CHORUS_ON, on); /* already on */

  bool any_audio = false;
  for (uint32_t i = 0; i < CHORUS_BUFFER_SIZE; i++) {
    any_audio |= (s.chorus.buffer[i] != 0.0f);
  }
  TEST_ASSERT_TRUE(any_audio);

  send(make_note_off(60));
  synth_set_param(&s, SYNTH_CHORUS_ON, off);
}

void test_cc93_switches_chorus_on_and_off(void) {
  send(make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_CHORUSLEVEL, 127));
  TEST_ASSERT_TRUE(s.chorus_enable);

  send(make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_CHORUSLEVEL, 0));
  TEST_ASSERT_FALSE(s.chorus_enable);
}

/*
 * CC109 toggles soft takeover and nothing else.  With takeover on, a cutoff
 * CC far from the patch value (default fully open) is held back.
 */
void test_cc109_toggles_soft_takeover_not_chorus(void) {
  send(make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_SOFT_TAKEOVER, 127));
  TEST_ASSERT_FALSE(s.chorus_enable);

  send(make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_FREQUENCYCUTOFF, 0));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, s.params[DCF_CUTOFF].f); /* held back */

  /* Takeover state is module-global - switch it off for the other tests */
  send(make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_SOFT_TAKEOVER, 0));
  send(make_msg(MIDI_STATUS_CONTROL_CHANGE, MIDI_CC_FREQUENCYCUTOFF, 0));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, s.params[DCF_CUTOFF].f);
}

int main(void) {
  UNITY_BEGIN();

  /* Initialisation */
  RUN_TEST(test_init_midi_channel_is_omni);
  RUN_TEST(test_init_all_voice_idle);
  RUN_TEST(test_init_params_pointer_set_on_all_voice);

  /* Free-voice allocation */
  RUN_TEST(test_note_on_activates_one_voice);
  RUN_TEST(test_note_on_stores_correct_note);
  RUN_TEST(test_two_note_on_activates_two_voice);
  RUN_TEST(test_eight_note_on_fills_all_voice);

  /* Retrigger */
  RUN_TEST(test_retrigger_does_not_add_a_second_voice);
  RUN_TEST(test_retrigger_updates_velocity);

  /* Steal */
  RUN_TEST(test_fifth_note_on_steals_oldest_voice);
  RUN_TEST(test_fifth_note_steal_target_is_stealing_state);
  RUN_TEST(test_steal_picks_oldest_not_youngest);

  /* Steal priority */
  RUN_TEST(test_steal_prefers_a_releasing_voice_over_the_objectively_oldest);
  RUN_TEST(
      test_steal_prefers_a_sustained_voice_over_a_non_sustained_alternative);
  RUN_TEST(
      test_steal_falls_back_to_a_sustained_voice_when_nothing_else_is_available);
  RUN_TEST(test_stealing_a_sustained_voice_clears_sustained_for_the_new_note);
  RUN_TEST(test_note_off_arriving_during_a_steal_is_not_lost);

  /* Ageing */
  RUN_TEST(
      test_a_voice_that_just_finished_stealing_is_not_immediately_re_stolen);
  RUN_TEST(test_age_increments_on_new_allocation);
  RUN_TEST(test_age_not_incremented_on_retrigger);

  /* Note-off */
  RUN_TEST(test_note_off_sets_release_on_matching_voice);
  RUN_TEST(test_note_off_for_unknown_note_is_harmless);
  RUN_TEST(test_note_off_does_not_affect_other_voice);

  /* Vel-zero = note-off */
  RUN_TEST(test_note_on_velocity_zero_acts_as_note_off);

  /* CC dispatch */
  RUN_TEST(test_cc_message_does_not_crash);

  /* MIDI channel masking */
  RUN_TEST(test_note_on_is_handled_on_non_first_channel_in_omni_mode);
  RUN_TEST(test_control_change_is_handled_on_non_first_channel_in_omni_mode);

  /* Pitch bend */
  RUN_TEST(test_pitch_bend_center_is_near_zero_semitones);
  RUN_TEST(test_pitch_bend_full_down_reaches_negative_bend_range);
  RUN_TEST(test_pitch_bend_full_up_approaches_positive_bend_range);
  RUN_TEST(test_pitch_bend_propagates_to_active_voice);

  RUN_TEST(test_mod_wheel_cc_sets_normalized_value);
  RUN_TEST(test_mod_wheel_zero_cc_gives_zero);
  RUN_TEST(test_mod_wheel_propagates_to_active_voice);
  RUN_TEST(test_mod_wheel_dest_defaults_to_vco);

  /* Program change */
  RUN_TEST(test_program_change_loads_resolved_patch);
  RUN_TEST(test_program_change_falls_back_to_default_patch_when_unresolved);
  RUN_TEST(test_program_change_with_no_loader_wired_falls_back_to_default);
  RUN_TEST(test_load_patch_poly_to_mono_applies_voice_params);
  RUN_TEST(test_load_patch_mono_to_poly_applies_voice_params);
  /* Sustain pedal */
  RUN_TEST(test_sustain_defers_poly_release_until_pedal_up);
  RUN_TEST(test_sustain_does_not_affect_notes_without_pedal);
  RUN_TEST(test_sustain_defers_mono_release_until_pedal_up);

  /* Sostenuto pedal */
  RUN_TEST(test_sostenuto_defers_release_only_for_notes_sounding_at_press);
  RUN_TEST(test_sostenuto_does_not_affect_notes_without_pedal);
  RUN_TEST(test_sostenuto_and_sustain_both_held_require_both_released);
  RUN_TEST(test_sostenuto_defers_mono_release_only_for_note_sounding_at_press);
  RUN_TEST(test_sostenuto_does_not_hold_a_mono_note_started_after_press);
  RUN_TEST(test_sustain_repressed_note_survives_pedal_up);
  RUN_TEST(test_sustain_repressed_note_released_again_is_held_until_pedal_up);
  RUN_TEST(test_sostenuto_repressed_note_survives_pedal_up);
  RUN_TEST(test_sostenuto_repressed_note_released_again_is_still_held);

  /* Chorus and soft takeover CCs (C8) */
  RUN_TEST(test_cc93_switches_chorus_on_and_off);
  RUN_TEST(test_chorus_delay_line_is_clear_when_switched_on);
  RUN_TEST(test_chorus_delay_line_is_kept_while_chorus_stays_on);
  RUN_TEST(test_cc109_toggles_soft_takeover_not_chorus);

  /* Voice steal edge cases (C5) */
  RUN_TEST(test_mono_legato_during_steal_starts_the_held_note);
  RUN_TEST(test_mono_short_note_during_steal_is_played_then_released);
  RUN_TEST(test_mono_new_phrase_during_steal_retargets_the_steal);
  RUN_TEST(test_mono_pedal_up_during_steal_does_not_leave_a_stuck_note);
  RUN_TEST(test_mono_steal_keeps_unison_detune);
  RUN_TEST(test_poly_pedal_up_during_steal_race_does_not_stick_note);

  /* Unison detune */
  RUN_TEST(test_unison_detune_spreads_mono_voices_around_the_note);
  RUN_TEST(test_unison_detune_stays_zero_in_poly_mode);

  /* Channel mode messages (D2) */
  RUN_TEST(test_cc123_releases_held_keys);
  RUN_TEST(test_cc123_respects_the_sustain_pedal);
  RUN_TEST(test_cc120_silences_everything_at_once);
  RUN_TEST(test_cc121_lifts_pedals_and_centres_controllers);
  RUN_TEST(test_cc126_and_cc127_select_the_voice_mode);
  RUN_TEST(test_cc124_and_cc125_act_as_all_notes_off);
  RUN_TEST(test_cc127_no_longer_changes_a_parameter_by_value);
  RUN_TEST(test_system_reset_returns_to_rest);

  /* Portamento */
  RUN_TEST(test_portamento_first_mono_note_does_not_glide);
  RUN_TEST(test_portamento_legato_glides_toward_target_over_several_blocks);
  RUN_TEST(test_portamento_disabled_jumps_immediately_on_legato);

  /* synth_render() */
  RUN_TEST(test_advance_idle_voice_stay_idle);
  RUN_TEST(test_advance_active_voice_does_not_become_idle_without_note_off);

  return UNITY_END();
}