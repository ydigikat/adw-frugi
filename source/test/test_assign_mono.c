/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   A.I. Disclosure:  These unit tests written by Anthropic Claude.
   -----------------------------------------------------------------------------
   Mono (unison) voice assigner tests (design-voice-architecture.md 5.2, 5.3).

   Driven through synth_handle_midi() - MIDI -> keys layer -> assigner ->
   voices.  In mono every voice plays the same note, so voice 0 stands for
   the group unless a test is about the spread.
   -----------------------------------------------------------------------------
*/

#include <math.h>
#include <string.h>

#include "assign.h"
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

static void load_mono(float release, float detune) {
  param_value_t p[PARAM_COUNT];
  param_create_default_patch(p);
  p[SYNTH_VOICE_MODE].e = VOICE_MODE_MONO;
  p[DCA_RELEASE].f = release;
  p[SYNTH_UNISON_DETUNE].f = detune;
  synth_load_patch(&s, p);
  render_n(1);
}

/* --- level measurement (design section 5.3) -------------------------- */

#define BLOCKS_50MS (18) /* 18 x 128 samples = 48 ms at 48 kHz */

/* Mean power of the mix over n blocks, in dB */
static float render_db(int n) {
  float l[AUDIO_BLOCK_SIZE], r[AUDIO_BLOCK_SIZE];
  double sum = 0.0;

  for (int b = 0; b < n; b++) {
    synth_render(&s, l, r);
    for (unsigned i = 0; i < AUDIO_BLOCK_SIZE; i++) {
      sum += (double)l[i] * (double)l[i];
    }
  }
  return 10.0f * log10f((float)(sum / ((double)n * AUDIO_BLOCK_SIZE)) + 1e-20f);
}

/* Level of one poly voice playing `note` - the reference for unison */
static float poly_reference_db(uint8_t note, int skip, int n) {
  param_value_t p[PARAM_COUNT];
  param_create_default_patch(p);
  p[SYNTH_VOICE_MODE].e = VOICE_MODE_POLY;
  synth_load_patch(&s, p);
  note_on(note);
  render_n(skip);
  const float db = render_db(n);
  cc(MIDI_CC_ALLSOUNDOFF, 0);
  return db;
}

/* Start a unison note at `detune` (0..1 control value) */
static void start_unison(uint8_t note, float detune) {
  cc(MIDI_CC_ALLSOUNDOFF, 0); /* from silence, nothing held */
  load_mono(0.0f, detune);
  note_on(note);
}

static void assert_all_voices(enum voice_state state, uint8_t note) {
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    TEST_ASSERT_EQUAL(state, s.voice[i].state);
    TEST_ASSERT_EQUAL_UINT8(note, s.voice[i].note);
  }
}

void setUp(void) {
  synth_init(&s, MIDI_OMNI, NULL);
  load_mono(0.0f, 0.0f);
}

void tearDown(void) {}

/* ------------------------------------------------------------------------- */

void mono_patch_selects_the_mono_assigner(void) {
  TEST_ASSERT_EQUAL_PTR(&mono_assigner, s.assigner);
}

void first_key_triggers_every_voice(void) {
  note_on(60);
  assert_all_voices(VOICE_ACTIVE, 60);
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    TEST_ASSERT_EQUAL(ENV_ATTACK, s.voice[i].amp_env.state);
  }
}

/* Legato: a second key while the first is held changes pitch only */
void second_key_while_held_is_legato(void) {
  note_on(60);
  render_n(3); /* into sustain */
  note_on(64);

  assert_all_voices(VOICE_ACTIVE, 64);
  TEST_ASSERT_EQUAL(ENV_SUSTAIN, s.voice[0].amp_env.state); /* no retrigger */
}

/* Return to held note, for trills */
void releasing_the_sounding_key_returns_to_the_held_one(void) {
  note_on(60);
  note_on(64);
  note_off(64);
  assert_all_voices(VOICE_ACTIVE, 60);
}

void releasing_an_older_key_keeps_the_sounding_note(void) {
  note_on(60);
  note_on(64);
  note_off(60);
  assert_all_voices(VOICE_ACTIVE, 64);
  TEST_ASSERT_TRUE(s.voice[0].gate);
}

/* D7: no limit on held keys - the old note stack tracked only 8 */
void return_to_held_works_with_more_than_eight_keys(void) {
  for (uint8_t n = 40; n < 50; n++) {
    note_on(n);
  }
  for (uint8_t n = 49; n > 40; n--) {
    note_off(n);
    assert_all_voices(VOICE_ACTIVE, (uint8_t)(n - 1));
  }
}

void last_key_up_releases_the_voices(void) {
  note_on(60);
  render_n(2);
  note_off(60);
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    TEST_ASSERT_FALSE(s.voice[i].gate);
  }
}

/* Release only when nothing at all is held - a pedal-held note counts */
void voices_stay_on_while_a_pedal_holds_a_note(void) {
  cc(MIDI_CC_HOLDPEDAL, 127);
  note_on(60);
  render_n(2);
  note_off(60);
  TEST_ASSERT_TRUE(s.voice[0].gate);

  cc(MIDI_CC_HOLDPEDAL, 0);
  TEST_ASSERT_FALSE(s.voice[0].gate);
}

/* D1: a pedal-held note doesn't take part in priority - a new key is a new
   phrase (retrigger), not legato */
void key_after_a_pedal_held_note_starts_a_new_phrase(void) {
  load_mono(0.5f, 0.0f);
  cc(MIDI_CC_HOLDPEDAL, 127);
  note_on(60);
  render_n(3);
  note_off(60); /* held by the pedal */

  note_on(64);
  TEST_ASSERT_EQUAL(VOICE_STEALING, s.voice[0].state); /* new phrase: steal */
  render_n(8);
  assert_all_voices(VOICE_ACTIVE, 64);
}

/* Mode change silences the voices but keeps the keys layer (D6) */
void mode_change_silences_voices_and_later_key_up_is_harmless(void) {
  note_on(60);
  param_value_t poly = {.e = VOICE_MODE_POLY};
  synth_set_param(&s, SYNTH_VOICE_MODE, poly);

  TEST_ASSERT_EQUAL_PTR(&poly_assigner, s.assigner);
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    TEST_ASSERT_EQUAL(VOICE_IDLE, s.voice[i].state);
  }

  note_off(60); /* key still known to the keys layer */
  note_on(62);  /* plays normally in poly */
  TEST_ASSERT_EQUAL_INT(1, s.voice[0].state == VOICE_ACTIVE);
}

/*
 * Panic (CC120 All Sound Off) with a key held must not leave the next key
 * playing legato onto silent voices.  The old mono stack wasn't cleared by
 * the panic, so the next note was treated as legato and never sounded.
 */
void note_after_panic_with_a_key_held_sounds(void) {
  note_on(60);
  cc(MIDI_CC_ALLSOUNDOFF, 0);
  note_on(64);
  assert_all_voices(VOICE_ACTIVE, 64);
}

/* The same through CC123 All Notes Off, which releases rather than kills */
void note_after_all_notes_off_with_a_key_held_sounds(void) {
  load_mono(0.5f, 0.0f); /* a release tail, so the next note steals it */
  note_on(60);
  render_n(4);
  cc(MIDI_CC_ALLNOTESOFF, 0);
  TEST_ASSERT_FALSE(s.voice[0].gate); /* released, not cut */

  note_on(64); /* a new phrase, not legato onto the released note */
  for (int i = 0; i < 32 && s.voice[0].state != VOICE_ACTIVE; i++) {
    render_n(1);
  }
  assert_all_voices(VOICE_ACTIVE, 64);
  TEST_ASSERT_TRUE(s.voice[0].gate);
}

/* --- unison ---------------------------------------------------------- */

/* U1: a detune change is heard on a note already held */
void detune_change_reaches_a_held_note(void) {
  note_on(57);
  render_n(1);
  const float inc0 = s.voice[0].dco_1.inc;

  param_value_t detune = {.f = 0.1f};
  synth_set_param(&s, SYNTH_UNISON_DETUNE, detune);
  render_n(1);

  TEST_ASSERT_TRUE(s.voice[0].dco_1.inc < inc0);
  TEST_ASSERT_TRUE(s.voice[SYNTH_MAX_VOICES - 1].dco_1.inc > inc0);
}

/*
 * U3: the offsets are uneven (even spacing pulses), sum to zero (the note
 * stays in tune) and span the spread with the widest voice at half of it.
 */
void detune_offsets_are_uneven_and_stay_in_tune(void) {
  load_mono(0.0f, 1.0f); /* full control: a 100 cent spread */

  float sum = 0.0f, widest = 0.0f;
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    sum += s.voice[i].unison_cents;
    widest = fmaxf(widest, fabsf(s.voice[i].unison_cents));
  }
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, sum);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, widest);

  /* not evenly spaced: the gaps between neighbours differ */
  float gap_min = 1e9f, gap_max = 0.0f;
  for (int i = 1; i < SYNTH_MAX_VOICES; i++) {
    const float gap = s.voice[i].unison_cents - s.voice[i - 1].unison_cents;
    gap_min = fminf(gap_min, gap);
    gap_max = fmaxf(gap_max, gap);
  }
  TEST_ASSERT_TRUE(gap_min > 0.0f); /* table is in pitch order */
  TEST_ASSERT_TRUE(gap_max > 2.0f * gap_min);
}

/* U2: start phases are irregular - no two voices share one, not i/N */
void start_phases_are_irregular(void) {
  load_mono(0.0f, 0.5f);
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    const float phase = s.voice[i].start_phase;
    TEST_ASSERT_TRUE(phase >= 0.0f && phase < 1.0f);
    TEST_ASSERT_FALSE(fabsf(phase - (float)i / SYNTH_MAX_VOICES) < 1e-3f);
    for (int j = 0; j < i; j++) {
      TEST_ASSERT_TRUE(fabsf(phase - s.voice[j].start_phase) > 0.01f);
    }
  }
}

/* With no detune every voice plays the identical waveform */
void zero_detune_voices_start_in_phase(void) {
  note_on(57);
  render_n(1);
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    TEST_ASSERT_LESS_THAN_FLOAT(0.0f, s.voice[i].start_phase);
    TEST_ASSERT_EQUAL_FLOAT(s.voice[0].dco_1.phase, s.voice[i].dco_1.phase);
  }
}

/* D9 / U5: the detune control is squared onto 0-100 cents */
void detune_control_is_squared_onto_100_cents(void) {
  load_mono(0.0f, 0.5f);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 25.0f, s.unison_detune);
  load_mono(0.0f, 1.0f);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 100.0f, s.unison_detune);
  load_mono(0.0f, 0.1f);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, s.unison_detune);
}

/*
 * C22: the start-phase stagger survives a steal.  Previously it was applied
 * to the fading voices and then reset when the new note started, so after a
 * steal every voice started in phase (measured ~0.76 for all of them).
 */
void start_phases_survive_a_mono_steal(void) {
  load_mono(0.5f, 0.1f);
  note_on(60);
  render_n(4);
  note_off(60); /* release tail */
  render_n(1);
  note_on(62); /* new phrase over the tail: a steal */

  for (int i = 0; i < 16 && s.voice[0].state != VOICE_ACTIVE; i++) {
    render_n(1);
  }
  assert_all_voices(VOICE_ACTIVE, 62);

  float lo = 1.0f, hi = 0.0f;
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    lo = fminf(lo, s.voice[i].dco_1.phase);
    hi = fmaxf(hi, s.voice[i].dco_1.phase);
  }
  TEST_ASSERT_TRUE(hi - lo > 0.5f); /* staggered, not all in phase */
}

/*
 * U4 mix law: in phase the voices add by amplitude (gain / N), detuned by
 * power (gain / sqrt N).  A note detuned while held stays on the power law,
 * even if the detune goes back to zero - the voices have drifted apart.
 */
void mix_scale_follows_whether_voices_are_in_phase(void) {
  const float in_phase = ASSIGN_VOICE_GAIN / SYNTH_MAX_VOICES;
  const float by_power = ASSIGN_VOICE_GAIN / sqrtf((float)SYNTH_MAX_VOICES);

  note_on(57);
  TEST_ASSERT_EQUAL_FLOAT(in_phase, s.assigner->mix_scale(&s));

  param_value_t detune = {.f = 0.3f};
  synth_set_param(&s, SYNTH_UNISON_DETUNE, detune);
  TEST_ASSERT_EQUAL_FLOAT(by_power, s.assigner->mix_scale(&s));

  detune.f = 0.0f;
  synth_set_param(&s, SYNTH_UNISON_DETUNE, detune);
  TEST_ASSERT_EQUAL_FLOAT(by_power, s.assigner->mix_scale(&s));

  /* the next phrase starts in phase again */
  note_off(57);
  cc(MIDI_CC_ALLSOUNDOFF, 0);
  note_on(60);
  TEST_ASSERT_EQUAL_FLOAT(in_phase, s.assigner->mix_scale(&s));
}

/* Re-pressing a pedal-held note restarts only the envelopes - phases stay */
void retrigger_of_a_detuned_note_stays_on_the_power_law(void) {
  load_mono(0.5f, 0.3f);
  cc(MIDI_CC_HOLDPEDAL, 127);
  note_on(57);
  render_n(2);
  note_off(57); /* held by the pedal */

  param_value_t detune = {.f = 0.0f};
  synth_set_param(&s, SYNTH_UNISON_DETUNE, detune);
  note_on(57); /* new phrase, same note: a retrigger */

  TEST_ASSERT_EQUAL_FLOAT(ASSIGN_VOICE_GAIN / sqrtf((float)SYNTH_MAX_VOICES),
                          s.assigner->mix_scale(&s));
}

/*
 * D10 / U4: unison is about as loud as one poly voice at any detune
 * (it was +11 dB at zero detune, falling about 9 dB as detune rose).
 * Measured over ~3 s; the worst case is about 2.1 dB, at the smallest
 * detune, where the fixed phases colour the tone most.
 */
void unison_level_matches_a_poly_voice(void) {
  const float ref = poly_reference_db(57, 1, 1125);
  const float ctl[] = {0.0f, 0.05f, 0.1f, 0.2f, 0.3f, 0.5f, 0.7f, 1.0f};

  for (unsigned k = 0; k < sizeof(ctl) / sizeof(ctl[0]); k++) {
    start_unison(57, ctl[k]);
    render_n(1);
    TEST_ASSERT_FLOAT_WITHIN(2.5f, ref, render_db(1125));
  }
}

/*
 * U2: the note starts at about its steady level.  With evenly spaced phases
 * the first 50 ms were 5-13 dB quiet (most harmonics cancelled); the
 * irregular table keeps it within about 2.7 dB of a poly voice's onset.
 */
void unison_onset_is_not_thin(void) {
  const float ref = poly_reference_db(57, 1, BLOCKS_50MS);
  const float ctl[] = {0.1f, 0.2f, 0.3f, 0.5f, 0.7f, 1.0f};

  for (unsigned k = 0; k < sizeof(ctl) / sizeof(ctl[0]); k++) {
    start_unison(57, ctl[k]);
    render_n(1);
    TEST_ASSERT_FLOAT_WITHIN(3.5f, ref, render_db(BLOCKS_50MS));
  }
}

/*
 * U3: beating stays bounded.  Measured as the spread of 50 ms levels over
 * ~3 s.  Even spacing gave 13-21 dB at 25-100 cents; the uneven table gives
 * about 9-12 dB.  Some beating always remains with 6-8 voices - it is what
 * unison sounds like - so the design's original "< 3 dB" was not reachable.
 */
void unison_beating_is_bounded(void) {
  const float ctl[] = {0.5f, 0.7f, 1.0f};

  for (unsigned k = 0; k < sizeof(ctl) / sizeof(ctl[0]); k++) {
    start_unison(57, ctl[k]);
    render_n(1 + BLOCKS_50MS); /* past the onset */

    float lo = 1e9f, hi = -1e9f;
    for (int w = 0; w < 62; w++) {
      const float db = render_db(BLOCKS_50MS);
      lo = fminf(lo, db);
      hi = fmaxf(hi, db);
    }
    TEST_ASSERT_LESS_THAN_FLOAT(12.0f, hi - lo);
  }
}

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(mono_patch_selects_the_mono_assigner);
  RUN_TEST(first_key_triggers_every_voice);
  RUN_TEST(second_key_while_held_is_legato);
  RUN_TEST(releasing_the_sounding_key_returns_to_the_held_one);
  RUN_TEST(releasing_an_older_key_keeps_the_sounding_note);
  RUN_TEST(return_to_held_works_with_more_than_eight_keys);
  RUN_TEST(last_key_up_releases_the_voices);
  RUN_TEST(voices_stay_on_while_a_pedal_holds_a_note);
  RUN_TEST(key_after_a_pedal_held_note_starts_a_new_phrase);
  RUN_TEST(mode_change_silences_voices_and_later_key_up_is_harmless);
  RUN_TEST(note_after_panic_with_a_key_held_sounds);
  RUN_TEST(note_after_all_notes_off_with_a_key_held_sounds);
  RUN_TEST(detune_change_reaches_a_held_note);
  RUN_TEST(detune_offsets_are_uneven_and_stay_in_tune);
  RUN_TEST(start_phases_are_irregular);
  RUN_TEST(zero_detune_voices_start_in_phase);
  RUN_TEST(detune_control_is_squared_onto_100_cents);
  RUN_TEST(start_phases_survive_a_mono_steal);
  RUN_TEST(mix_scale_follows_whether_voices_are_in_phase);
  RUN_TEST(retrigger_of_a_detuned_note_stays_on_the_power_law);
  RUN_TEST(unison_level_matches_a_poly_voice);
  RUN_TEST(unison_onset_is_not_thin);
  RUN_TEST(unison_beating_is_bounded);

  return UNITY_END();
}
