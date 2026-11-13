/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
*/

#include "midi_parser.h"
#include "patch.h"
#include "unity.h"
#include "voice.h"
#include <math.h>
#include <string.h>

static struct voice v;
static float sample_block[AUDIO_BLOCK_SIZE];
static param_value_t params[PARAM_COUNT];

/* Drive voice_render() n times */
static void render_n(int n) {
  for (int i = 0; i < n; i++)
    voice_render(&v);
}

/*
 * Initialises a standalone voice (distinct from the shared fixture `v`)
 * with its own param snapshot, so two voices can be compared side by side -
 * see xmod_level_produces_an_audible_change_in_output().
 */
static void prime_voice(struct voice *voice, float block[],
                        param_value_t pvals[]) {
  voice_init(voice, block, pvals);
  voice->unison_cents = 0.0f;
  voice->pitch_bend_semitones = 0.0f;
  voice->mod_wheel = 0.0f;
  voice->portamento_enable = false;
  voice->portamento_coeff = 0.0f;
  voice_update(voice);
  voice_render(voice); /* consume the initial pending chain update */
}

void setUp(void) {
  memset(params, 0, sizeof(params));
  param_create_default_patch(params);
  /* Default patch: zero attack/decay/release, sustain=1 - each segment
     collapses to the next within a single render block (see test_env.c),
     so ATTACK -> DECAY takes 1 render and DECAY -> SUSTAIN takes 1 more. */

  voice_init(&v, sample_block, params);

  /* voice_kill() (called by voice_init()) only clears note/lifecycle
     data - these are performance fields normally re-broadcast by struct
     synth every block, so pin them here for test-to-test isolation. */
  v.unison_cents = 0.0f;
  v.pitch_bend_semitones = 0.0f;
  v.mod_wheel = 0.0f;
  v.portamento_enable = false;
  v.portamento_coeff = 0.0f;

  voice_update(&v);
  render_n(1); /* consume the initial pending chain update, load the chain */
}

void tearDown(void) {}

/* -------------------------------------------------------------------------
 * Initialisation / reset
 * ---------------------------------------------------------------------- */

void init_sets_idle_state_and_clears_note_data(void) {
  struct voice fresh;
  float block[AUDIO_BLOCK_SIZE];
  voice_init(&fresh, block, params);

  TEST_ASSERT_EQUAL(VOICE_IDLE, fresh.state);
  TEST_ASSERT_EQUAL_UINT8(0, fresh.note);
  TEST_ASSERT_EQUAL_UINT8(0, fresh.velocity);
  TEST_ASSERT_FALSE(fresh.gate);
}

void init_zeroes_the_sample_block(void) {
  float block[AUDIO_BLOCK_SIZE];
  for (size_t i = 0; i < AUDIO_BLOCK_SIZE; i++)
    block[i] = 123.0f;

  struct voice fresh;
  voice_init(&fresh, block, params);

  for (size_t i = 0; i < AUDIO_BLOCK_SIZE; i++)
    TEST_ASSERT_EQUAL_FLOAT(0.0f, block[i]);
}

void reset_returns_an_active_voice_to_idle(void) {
  voice_trigger(&v, 60, 100);
  render_n(1);
  TEST_ASSERT_EQUAL(VOICE_ACTIVE, v.state);

  voice_kill(&v);

  TEST_ASSERT_EQUAL(VOICE_IDLE, v.state);
  TEST_ASSERT_FALSE(v.gate);
}

/*
 * update_pending describes the voice's parameters, not its note, so a reset
 * must leave a requested chain update in place for the next render.
 */
void reset_preserves_a_pending_update(void) {
  voice_trigger(&v, 60, 100);
  render_n(1);

  voice_update(&v);
  voice_kill(&v);

  TEST_ASSERT_TRUE(v.update_pending);
}

void reset_pending_update_is_applied_on_next_render(void) {
  params[DCA_SUSTAIN].f = 0.5f;
  voice_update(&v);
  voice_kill(&v);
  render_n(1);

  TEST_ASSERT_FALSE(v.update_pending);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.5f, v.amp_env.sustain);
}

void init_clears_update_pending(void) {
  struct voice fresh;
  float block[AUDIO_BLOCK_SIZE];
  memset(&fresh, 0xFF, sizeof(fresh)); /* stack garbage, as an auto would be */
  voice_init(&fresh, block, params);

  TEST_ASSERT_FALSE(fresh.update_pending);
}

/* -------------------------------------------------------------------------
 * Note on - free voice (IDLE -> ACTIVE)
 * ---------------------------------------------------------------------- */

void note_on_from_idle_starts_immediately_and_moves_to_active(void) {
  voice_trigger(&v, 60, 100);

  /* No preemption in the super-loop - voice_trigger() always runs to
     completion between render() calls, so the note starts synchronously
     rather than waiting for the next render(). */
  TEST_ASSERT_EQUAL(VOICE_ACTIVE, v.state);
  TEST_ASSERT_EQUAL_UINT8(60, v.note);
  TEST_ASSERT_EQUAL_UINT8(100, v.velocity);
  TEST_ASSERT_EQUAL(ENV_ATTACK, v.amp_env.state);
}

void note_on_computes_pitch_from_midi_frequency_table(void) {
  voice_trigger(&v, 69, 100); /* A4 */
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, MIDI_FREQ_TABLE[69], v.pitch);
}

/*
 * Unison detune is applied with the pitch bend when the chain updates, not
 * baked into the note pitch, so check the rate the oscillator runs at.
 */
void note_on_applies_unison_cents_to_pitch(void) {
  v.unison_cents = 1200.0f; /* +1 octave */
  voice_update(&v);
  voice_trigger(&v, 69, 100);
  render_n(1);

  TEST_ASSERT_FLOAT_WITHIN(1e-3f, MIDI_FREQ_TABLE[69], v.pitch);
  TEST_ASSERT_FLOAT_WITHIN(
      1e-6f, MIDI_FREQ_TABLE[69] * 2.0f / AUDIO_SAMPLE_RATE, v.dco_1.inc);
}

/* U1: a detune change is heard on a note already sounding */
void unison_change_reaches_a_held_note(void) {
  voice_trigger(&v, 69, 100);
  render_n(1);
  const float inc_before = v.dco_1.inc;

  v.unison_cents = 1200.0f;
  voice_update(&v);
  render_n(1);

  TEST_ASSERT_FLOAT_WITHIN(1e-6f, inc_before * 2.0f, v.dco_1.inc);
}

/* The unison start phase is applied when a note starts */
void start_phase_is_applied_when_a_note_starts(void) {
  v.start_phase = 0.25f;
  voice_trigger(&v, 60, 100);

  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.25f, v.dco_1.phase);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.25f, v.dco_2.phase);
  v.start_phase = -1.0f;
}

void note_on_hands_pitch_to_the_dcos_immediately(void) {
  voice_trigger(&v, 60, 100);

  /* voice_start_chain() hands the note's pitch to both DCOs synchronously,
     no render() needed. */
  TEST_ASSERT_EQUAL_FLOAT(v.pitch, v.dco_1.pitch);
  TEST_ASSERT_EQUAL_FLOAT(v.pitch, v.dco_2.pitch);
}

void note_on_puts_envelope_into_attack_immediately(void) {
  voice_trigger(&v, 60, 100);
  TEST_ASSERT_EQUAL(ENV_ATTACK, v.amp_env.state); /* applied synchronously */

  render_n(1);

  /* Default patch has zero attack time - ATTACK collapses to full level
     and hands off to DECAY within the block that renders it. */
  TEST_ASSERT_EQUAL(ENV_DECAY, v.amp_env.state);
}

/* -------------------------------------------------------------------------
 * Note on - retrigger (ACTIVE + same note)
 * ---------------------------------------------------------------------- */

void retrigger_same_note_updates_velocity_immediately(void) {
  voice_trigger(&v, 60, 64);
  render_n(1);

  voice_trigger(&v, 60, 127);

  TEST_ASSERT_EQUAL(VOICE_ACTIVE, v.state);
  TEST_ASSERT_EQUAL_UINT8(127, v.velocity);
  TEST_ASSERT_EQUAL(ENV_ATTACK,
                    v.amp_env.state); /* retrigger applied synchronously */
}

/*
 * The gate follows trigger and gate-off; a retrigger after a gate-off raises
 * it again.  (Pedal holds are the keys layer's job - see test_keys.c.)
 */
void gate_follows_trigger_and_gate_off(void) {
  voice_trigger(&v, 60, 64);
  TEST_ASSERT_TRUE(v.gate);
  render_n(1);

  voice_gate_off(&v);
  TEST_ASSERT_FALSE(v.gate);
  TEST_ASSERT_EQUAL(ENV_RELEASE, v.amp_env.state);

  voice_trigger(&v, 60, 64);
  TEST_ASSERT_TRUE(v.gate);
  TEST_ASSERT_EQUAL(ENV_ATTACK, v.amp_env.state);
}

void retrigger_resets_envelope_back_to_attack(void) {
  voice_trigger(&v, 60, 64);
  render_n(2); /* ATTACK -> DECAY -> SUSTAIN, one collapse per block */
  TEST_ASSERT_EQUAL(ENV_SUSTAIN, v.amp_env.state);

  voice_trigger(&v, 60, 127); /* retrigger - applied synchronously */
  TEST_ASSERT_EQUAL(ENV_ATTACK, v.amp_env.state);

  render_n(1);

  /* Zero attack time collapses ATTACK straight to DECAY within this
     block. A retrigger that had been ignored would have left the
     envelope sitting in SUSTAIN. */
  TEST_ASSERT_EQUAL(ENV_DECAY, v.amp_env.state);
}

/* -------------------------------------------------------------------------
 * Note on - steal (ACTIVE + different note, no free voice)
 * ---------------------------------------------------------------------- */

void note_on_different_note_while_active_starts_the_steal(void) {
  voice_trigger(&v, 60, 100);
  render_n(1);

  voice_trigger(&v, 67, 90);

  TEST_ASSERT_EQUAL(VOICE_STEALING, v.state);
  TEST_ASSERT_EQUAL(ENV_SHUTDOWN,
                    v.amp_env.state); /* env_rtz() applied synchronously */
  TEST_ASSERT_EQUAL_UINT8(67, v.target_note);
  TEST_ASSERT_EQUAL_UINT8(90, v.target_velocity);
  TEST_ASSERT_EQUAL_UINT8(60, v.note); /* old note still playing during RTZ */
}

void steal_requested_before_the_stolen_notes_first_render_still_completes(
    void) {
  /* Two note_on messages processed back-to-back with no voice_render() in
     between - e.g. a fast run of notes filling every voice in the same
     MIDI burst. voice_trigger() applies env_note_on() synchronously, so
     the first note's envelope is already in ATTACK but still at level
     0.0 - it hasn't been rendered even once. Without the ENV_OFF fix in
     env_rtz() (see test_env.c), this leaves the voice stuck in
     VOICE_STEALING forever: the old note never releases (it's no longer
     ACTIVE, so note_off can't find it) and the new note never sounds. */
  voice_trigger(&v, 60, 100);
  voice_trigger(&v, 67, 90);

  int guard = 0;
  while (v.state == VOICE_STEALING && guard++ < 40) {
    voice_render(&v);
  }

  TEST_ASSERT_EQUAL(VOICE_ACTIVE, v.state);
  TEST_ASSERT_EQUAL_UINT8(67, v.note);
}

/*
 * A trigger on a voice mid-steal retargets the steal: the fade already
 * under way continues, and the new note is the one that starts.  (Every
 * call is valid in every state; whether a mid-steal voice should be
 * retargeted is the assigner's decision - poly never does it.)
 */
void trigger_while_stealing_retargets_the_steal(void) {
  voice_trigger(&v, 60, 100);
  render_n(1);
  voice_trigger(&v, 67, 90); /* -> STEALING */
  const float level = v.amp_env.level;

  voice_trigger(&v, 72, 80);

  TEST_ASSERT_EQUAL(VOICE_STEALING, v.state);
  TEST_ASSERT_EQUAL(ENV_SHUTDOWN, v.amp_env.state); /* fade continues */
  TEST_ASSERT_EQUAL_FLOAT(level, v.amp_env.level);
  TEST_ASSERT_EQUAL_UINT8(72, v.target_note);
  TEST_ASSERT_EQUAL_UINT8(80, v.target_velocity);
}

void steal_completes_and_switches_to_the_new_note(void) {
  voice_trigger(&v, 60, 100);
  render_n(1);
  voice_trigger(&v, 67, 90);

  /* RTZ fades the envelope to OFF, then the voice swaps in the steal
     note and returns to ACTIVE - a handful of blocks is plenty. */
  render_n(20);

  TEST_ASSERT_EQUAL(VOICE_ACTIVE, v.state);
  TEST_ASSERT_EQUAL_UINT8(67, v.note);
  TEST_ASSERT_EQUAL_UINT8(90, v.velocity);
}

void steal_starts_the_new_note_in_the_same_block_the_rtz_completes(void) {
  voice_trigger(&v, 60, 100);
  render_n(1);
  voice_trigger(&v, 67, 90); /* -> STEALING */

  /* Step renders one at a time until the RTZ fade finishes and the voice
     swaps back to ACTIVE. The very render() call that detects ENV_OFF
     must also start the new note - no extra silent block waiting for a
     deferred start. */
  int guard = 0;
  while (v.state == VOICE_STEALING && guard++ < 40) {
    voice_render(&v);
  }

  TEST_ASSERT_EQUAL(VOICE_ACTIVE, v.state);
  TEST_ASSERT_EQUAL_UINT8(67, v.note);
  /* voice_start_chain() must already have run - the DCOs are on the new
     note's pitch, not left at the old (silent) one. Not asserting
     ENV_ATTACK directly since the default patch's zero attack time can
     collapse it to DECAY within this same render. */
  TEST_ASSERT_NOT_EQUAL(ENV_OFF, v.amp_env.state);
  TEST_ASSERT_EQUAL_FLOAT(v.pitch, v.dco_1.pitch);
}

/* -------------------------------------------------------------------------
 * Note off / release
 * ---------------------------------------------------------------------- */

void note_off_moves_envelope_to_release_immediately(void) {
  voice_trigger(&v, 60, 100);
  render_n(1);

  voice_gate_off(&v);

  TEST_ASSERT_EQUAL(ENV_RELEASE, v.amp_env.state);
}

void note_off_release_uses_the_configured_release_time(void) {
  /* Default patch has zero release time, which collapses RELEASE -> OFF
     within the same block (see idle_voice tests below) - give this one a
     non-zero release so RELEASE is actually observable across renders. */
  params[DCA_RELEASE].f = 0.5f;
  voice_update(&v);
  render_n(1); /* apply the new release time to the chain */

  voice_trigger(&v, 60, 100);
  render_n(2); /* -> SUSTAIN */

  voice_gate_off(&v);

  TEST_ASSERT_EQUAL(ENV_RELEASE, v.amp_env.state);
}

void voice_returns_to_idle_once_envelope_finishes(void) {
  voice_trigger(&v, 60, 100);
  render_n(1);
  voice_gate_off(&v);

  /* Default patch has zero release time - RELEASE collapses to OFF in
     the very next block, then the ENV_OFF check on the block after that
     returns the voice to IDLE. */
  render_n(2);

  TEST_ASSERT_EQUAL(VOICE_IDLE, v.state);
  TEST_ASSERT_FALSE(v.update_pending);
}

void idle_voice_after_note_end_has_silent_sample_block(void) {
  voice_trigger(&v, 60, 100);
  render_n(1);
  voice_gate_off(&v);
  render_n(2); /* -> IDLE */

  render_n(1); /* idle render must not resurrect audio */

  for (size_t i = 0; i < AUDIO_BLOCK_SIZE; i++)
    TEST_ASSERT_EQUAL_FLOAT(0.0f, sample_block[i]);
}

/* -------------------------------------------------------------------------
 * Legato (mono mode note changes without retriggering the envelope)
 * ---------------------------------------------------------------------- */

void legato_changes_note_and_pitch_without_touching_envelope(void) {
  voice_trigger(&v, 60, 100);
  render_n(1);
  enum env_state state_before = v.amp_env.state;

  voice_set_note(&v, 67);

  TEST_ASSERT_EQUAL_UINT8(67, v.note);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, MIDI_FREQ_TABLE[67], v.pitch);
  TEST_ASSERT_EQUAL(state_before,
                    v.amp_env.state); /* legato never touches the envelope */
}

void legato_without_portamento_snaps_dco_pitch_immediately(void) {
  voice_trigger(&v, 60, 100);
  render_n(1);

  v.portamento_enable = false;
  voice_set_note(&v, 72);

  TEST_ASSERT_EQUAL_FLOAT(v.pitch, v.dco_1.pitch);
}

void legato_with_portamento_leaves_dco_pitch_for_the_glide_step(void) {
  voice_trigger(&v, 60, 100);
  render_n(1);
  float pitch_before = v.dco_1.pitch;

  v.portamento_enable = true;
  v.portamento_coeff = 0.01f; /* slow glide */
  voice_set_note(&v, 72);
  render_n(1);

  /* voice_run_chain() eases dco pitch toward v.pitch rather than jumping */
  TEST_ASSERT_TRUE(v.dco_1.pitch > pitch_before);
  TEST_ASSERT_TRUE(v.dco_1.pitch < v.pitch);
}

/* -------------------------------------------------------------------------
 * Update
 * ---------------------------------------------------------------------- */

void update_while_idle_reloads_the_chain_without_changing_state(void) {
  TEST_ASSERT_EQUAL(VOICE_IDLE, v.state);

  voice_update(&v);
  render_n(1);

  TEST_ASSERT_EQUAL(VOICE_IDLE, v.state);
  TEST_ASSERT_FALSE(v.update_pending);
}

void update_while_active_applies_new_params_without_interrupting_the_note(
    void) {
  voice_trigger(&v, 60, 100);
  render_n(1);
  const float level_at_full = v.dco_1.level; /* default DCO1_LEVEL is 1.0 */

  param_value_t updated[PARAM_COUNT];
  memcpy(updated, params, sizeof(updated));
  updated[DCO1_LEVEL].f = 0.25f;
  v.params = updated;

  voice_update(&v);
  render_n(1);

  TEST_ASSERT_EQUAL(VOICE_ACTIVE, v.state);
  TEST_ASSERT_EQUAL_UINT8(60, v.note);
  /* The level setting reaches the DCO mixer; the render amplitude (fixed
     headroom x waveform gain) is unchanged by it */
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.25f, v.dco_1.mix_level);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, level_at_full, v.dco_1.level);
  v.params = params;
}

/* -------------------------------------------------------------------------
 * Mod wheel (CS-20M style VCO / VCO+VCF / VCF destination switch)
 * ---------------------------------------------------------------------- */

/*
 * Routes LFO1_RAMP (rate 0 -> frozen phase 0 -> constant 1.0 output, see
 * lfo_ramp()) into DCO1/DCO2 pitch, DCF cutoff and DCA level at depth 0.5,
 * and selects the given mod wheel destination. Gives a fully deterministic
 * mod_dest[] baseline (0.5 on every routed destination) to scale from.
 */
static void arm_mod_wheel_fixture(enum mod_wheel_dest dest) {
  /* static: v.params keeps pointing at this after we return, and the
     caller renders again afterwards to read v.mod_dest[] */
  static param_value_t updated[PARAM_COUNT];
  memcpy(updated, params, sizeof(updated));
  updated[LFO1_RATE].f = 0.0f;
  updated[DCO1_MOD_SOURCE].e = LFO1_RAMP;
  updated[DCO1_MOD_DEPTH].f = 0.5f;
  updated[DCO2_MOD_SOURCE].e = LFO1_RAMP;
  updated[DCO2_MOD_DEPTH].f = 0.5f;
  updated[DCF_MOD_SOURCE].e = LFO1_RAMP;
  updated[DCF_MOD_DEPTH].f = 0.5f;
  updated[DCA_MOD_SOURCE].e = LFO1_RAMP;
  updated[DCA_MOD_DEPTH].f = 0.5f;
  updated[SYNTH_MOD_WHEEL_DEST].e = dest;
  v.params = updated;

  voice_update(&v);
  voice_trigger(&v, 60, 100);
  render_n(1); /* applies the chain update and the note-on together */
}

void mod_wheel_at_zero_leaves_every_destination_at_its_patched_depth(void) {
  arm_mod_wheel_fixture(MOD_WHEEL_DEST_VCO_VCF);

  v.mod_wheel = 0.0f;
  render_n(1);

  TEST_ASSERT_EQUAL_FLOAT(0.5f, v.mod_dest[MOD_DEST_DCO1_PITCH]);
  TEST_ASSERT_EQUAL_FLOAT(0.5f, v.mod_dest[MOD_DEST_DCO2_PITCH]);
  TEST_ASSERT_EQUAL_FLOAT(0.5f, v.mod_dest[MOD_DEST_DCF_CUTOFF]);
  TEST_ASSERT_EQUAL_FLOAT(0.5f, v.mod_dest[MOD_DEST_DCA_LEVEL]);
}

void mod_wheel_vco_switch_boosts_dco_pitch_only(void) {
  arm_mod_wheel_fixture(MOD_WHEEL_DEST_VCO);

  v.mod_wheel = 1.0f;
  render_n(1);

  TEST_ASSERT_FLOAT_WITHIN(
      1e-6f, 1.0f, v.mod_dest[MOD_DEST_DCO1_PITCH]); /* 0.5 * (1 + 1*1) */
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, v.mod_dest[MOD_DEST_DCO2_PITCH]);
  TEST_ASSERT_EQUAL_FLOAT(0.5f,
                          v.mod_dest[MOD_DEST_DCF_CUTOFF]); /* untouched */
}

void mod_wheel_vcf_switch_boosts_dcf_cutoff_only(void) {
  arm_mod_wheel_fixture(MOD_WHEEL_DEST_VCF);

  v.mod_wheel = 1.0f;
  render_n(1);

  TEST_ASSERT_EQUAL_FLOAT(0.5f,
                          v.mod_dest[MOD_DEST_DCO1_PITCH]); /* untouched */
  TEST_ASSERT_EQUAL_FLOAT(0.5f,
                          v.mod_dest[MOD_DEST_DCO2_PITCH]); /* untouched */
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, v.mod_dest[MOD_DEST_DCF_CUTOFF]);
}

void mod_wheel_vco_vcf_switch_boosts_both(void) {
  arm_mod_wheel_fixture(MOD_WHEEL_DEST_VCO_VCF);

  v.mod_wheel = 1.0f;
  render_n(1);

  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, v.mod_dest[MOD_DEST_DCO1_PITCH]);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, v.mod_dest[MOD_DEST_DCO2_PITCH]);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, v.mod_dest[MOD_DEST_DCF_CUTOFF]);
}

void mod_wheel_never_reaches_dca_level_regardless_of_switch_position(void) {
  enum mod_wheel_dest all_positions[] = {
      MOD_WHEEL_DEST_VCO, MOD_WHEEL_DEST_VCO_VCF, MOD_WHEEL_DEST_VCF};

  for (size_t i = 0; i < sizeof(all_positions) / sizeof(all_positions[0]);
       i++) {
    arm_mod_wheel_fixture(all_positions[i]);

    v.mod_wheel = 1.0f;
    render_n(1);

    TEST_ASSERT_EQUAL_FLOAT(0.5f, v.mod_dest[MOD_DEST_DCA_LEVEL]);
  }
}

void mod_wheel_does_not_affect_pwm_destinations(void) {
  param_value_t updated[PARAM_COUNT];
  memcpy(updated, params, sizeof(updated));
  updated[LFO1_RATE].f = 0.0f;
  updated[DCO1_PWM_SOURCE].e = LFO1_RAMP;
  updated[DCO1_PWM_DEPTH].f = 0.5f;
  updated[SYNTH_MOD_WHEEL_DEST].e = MOD_WHEEL_DEST_VCO_VCF;
  v.params = updated;

  voice_update(&v);
  voice_trigger(&v, 60, 100);
  render_n(1);

  v.mod_wheel = 1.0f;
  render_n(1);

  TEST_ASSERT_EQUAL_FLOAT(0.5f, v.mod_dest[MOD_DEST_DCO1_PWM]);
}

/* -------------------------------------------------------------------------
 * Mod envelope (ENV1) - mirrors amp_env's note_on/note_off/env_update
 * wiring (see voice_trigger(), voice_gate_off(), voice_update_chain()).
 * Previously mod_env was only ever env_init()'d - never updated with its
 * patch params nor triggered by a note, so it sat permanently in ENV_OFF
 * with level 0, making ENV1_LEVEL useless as a mod source.
 * ---------------------------------------------------------------------- */

void note_on_triggers_mod_env_into_attack_immediately(void) {
  voice_trigger(&v, 60, 100);

  TEST_ASSERT_EQUAL(ENV_ATTACK, v.mod_env.state); /* applied synchronously */
}

void mod_env_level_reaches_env1_level_mod_source(void) {
  voice_trigger(&v, 60, 100);

  /* Default patch has zero attack/decay and sustain=1 - reaches sustain
     within 2 renders, same timing as amp_env (see
     note_on_puts_envelope_into_attack_immediately above) */
  render_n(2);

  TEST_ASSERT_EQUAL_FLOAT(1.0f, v.mod_sources[ENV1_LEVEL]);
}

void note_off_releases_mod_env_same_as_amp_env(void) {
  voice_trigger(&v, 60, 100);
  render_n(2); /* reach sustain */

  voice_gate_off(&v);

  TEST_ASSERT_EQUAL(ENV_RELEASE, v.mod_env.state);
}

/* -------------------------------------------------------------------------
 * Cross-modulation (ring mod)
 * ---------------------------------------------------------------------- */

void xmod_level_produces_an_audible_change_in_output(void) {
  param_value_t params_off[PARAM_COUNT];
  memcpy(params_off, params, sizeof(params_off));
  params_off[DCO_RINGMOD_LEVEL].f = 0.0f;

  param_value_t params_on[PARAM_COUNT];
  memcpy(params_on, params, sizeof(params_on));
  params_on[DCO_RINGMOD_LEVEL].f = 1.0f;

  /* static: matches every other voice fixture in this file (see the
     shared `v`) - struct voice is large and several fields are only
     ever set by explicit init/update calls, so stack garbage in an
     unqualified local is a real risk if one is ever missed. */
  static struct voice voice_off, voice_on;
  static float block_off[AUDIO_BLOCK_SIZE];
  static float block_on[AUDIO_BLOCK_SIZE];

  prime_voice(&voice_off, block_off, params_off);
  prime_voice(&voice_on, block_on, params_on);

  voice_trigger(&voice_off, 60, 100);
  voice_trigger(&voice_on, 60, 100);

  /* Reach sustain (attack=decay=0 in the default patch) before comparing */
  for (int i = 0; i < 2; i++) {
    voice_render(&voice_off);
    voice_render(&voice_on);
  }

  double sumsq_diff = 0.0;
  for (size_t i = 0; i < AUDIO_BLOCK_SIZE; i++) {
    float d = block_on[i] - block_off[i];
    sumsq_diff += (double)d * (double)d;
  }
  float rms_diff = sqrtf((float)(sumsq_diff / AUDIO_BLOCK_SIZE));

  /* Regression guard: the ring term was previously crushed by both DCOs'
     fixed per-voice headroom squared (0.33*0.33), leaving DCO_XMOD_LEVEL
     with no perceptible effect on the output - see dco_cross_mod() and
     voice_run_chain()'s ring gain. */
  TEST_ASSERT_TRUE(rms_diff > 0.05f);
}

/*
 * Ring-mod spectrum checks.  A3 (220Hz) with DCO2 a fifth and 13 cents up
 * (~331Hz) - an inharmonic interval, so the product's sum and difference
 * frequencies land away from both input pitches.
 */
#define XMOD_NOTE (57)
#define XMOD_BLOCKS (256) /* 32768 samples, ~1.5Hz bins */
#define XMOD_LEN (XMOD_BLOCKS * AUDIO_BLOCK_SIZE)

static float xmod_capture[XMOD_LEN];

static void render_xmod(param_value_t pvals[], float xmod) {
  static struct voice voice;
  static float block[AUDIO_BLOCK_SIZE];

  pvals[DCO2_SEMI].i = 7;
  pvals[DCO2_CENTS].f = 0.63f; /* +13 cents */
  pvals[DCO_RINGMOD_LEVEL].f = xmod;

  prime_voice(&voice, block, pvals);
  voice_trigger(&voice, XMOD_NOTE, 100);
  for (int i = 0; i < 2; i++)
    voice_render(&voice); /* reach sustain */

  for (int b = 0; b < XMOD_BLOCKS; b++) {
    voice_render(&voice);
    memcpy(&xmod_capture[b * AUDIO_BLOCK_SIZE], block, sizeof(block));
  }
}

/* Hann-windowed Goertzel: power at freq_hz in xmod_capture */
static double tone_power(double freq_hz) {
  const double w = 2.0 * M_PI * freq_hz / AUDIO_SAMPLE_RATE;
  const double coeff = 2.0 * cos(w);
  double s1 = 0.0, s2 = 0.0;

  for (size_t i = 0; i < XMOD_LEN; i++) {
    double hann = 0.5 - 0.5 * cos(2.0 * M_PI * (double)i / (XMOD_LEN - 1));
    double s0 = (double)xmod_capture[i] * hann + coeff * s1 - s2;
    s2 = s1;
    s1 = s0;
  }
  return s1 * s1 + s2 * s2 - coeff * s1 * s2;
}

static double rms_capture(void) {
  double sumsq = 0.0;
  for (size_t i = 0; i < XMOD_LEN; i++)
    sumsq += (double)xmod_capture[i] * (double)xmod_capture[i];
  return sqrt(sumsq / XMOD_LEN);
}

/* The voice's own pitches, read back so tuning maths isn't duplicated */
static void xmod_pitches(double *f1, double *f2) {
  static struct voice probe;
  static float probe_block[AUDIO_BLOCK_SIZE];
  params[DCO2_SEMI].i = 7;
  params[DCO2_CENTS].f = 0.63f;
  prime_voice(&probe, probe_block, params);
  voice_trigger(&probe, XMOD_NOTE, 100);
  voice_render(&probe);
  *f1 = (double)probe.dco_1.inc * AUDIO_SAMPLE_RATE;
  *f2 = (double)probe.dco_2.inc * AUDIO_SAMPLE_RATE;
}

static double db(double power_ratio) { return 10.0 * log10(power_ratio); }

void full_xmod_with_dco1_muted_removes_both_input_pitches(void) {
  double f1, f2;
  xmod_pitches(&f1, &f2);

  /* Reference: each pitch at full level, dry */
  render_xmod(params, 0.0f);
  const double dry_f1 = tone_power(f1);
  const double dry_f2 = tone_power(f2);

  params[DCO1_LEVEL].f = 0.0f;
  render_xmod(params, 1.0f);

  /* Classic ring mod: both carriers at least 30dB down */
  TEST_ASSERT_TRUE(db(dry_f1 / tone_power(f1)) >= 30.0);
  TEST_ASSERT_TRUE(db(dry_f2 / tone_power(f2)) >= 30.0);

  /* ... while the difference tone (f2 - f1) is now present and strong */
  TEST_ASSERT_TRUE(tone_power(f2 - f1) > dry_f2 * 0.1);
}

void full_xmod_replaces_dco2_and_keeps_dco1(void) {
  double f1, f2;
  xmod_pitches(&f1, &f2);

  render_xmod(params, 0.0f);
  const double dry_f1 = tone_power(f1);
  const double dry_f2 = tone_power(f2);

  render_xmod(params, 1.0f);

  /* DCO2 is gone from its slot, DCO1 is untouched */
  TEST_ASSERT_TRUE(db(dry_f2 / tone_power(f2)) >= 30.0);
  TEST_ASSERT_TRUE(fabs(db(dry_f1 / tone_power(f1))) <= 0.5);
}

void ring_level_follows_dco2_level_not_dco1_level(void) {
  double f1, f2;
  xmod_pitches(&f1, &f2);

  params[DCO1_LEVEL].f = 0.0f;
  render_xmod(params, 1.0f);
  const double ring_ref = tone_power(f2 - f1);

  /* DCO1's level only adds dry DCO1, the ring is unchanged */
  params[DCO1_LEVEL].f = 0.5f;
  render_xmod(params, 1.0f);
  TEST_ASSERT_TRUE(fabs(db(ring_ref / tone_power(f2 - f1))) <= 0.5);

  /* DCO2's level is the ring level: half level, -6dB */
  params[DCO1_LEVEL].f = 0.0f;
  params[DCO2_LEVEL].f = 0.5f;
  render_xmod(params, 1.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.5, 6.02, db(ring_ref / tone_power(f2 - f1)));
}

void full_xmod_ring_is_about_as_loud_as_the_dco2_it_replaces(void) {
  /* DCO2 alone, dry */
  params[DCO1_LEVEL].f = 0.0f;
  render_xmod(params, 0.0f);
  const double dry = rms_capture();

  /* Ring alone */
  render_xmod(params, 1.0f);
  const double ring = rms_capture();

  /* Default saw x tri: within 3dB of DCO2 */
  TEST_ASSERT_TRUE(fabs(20.0 * log10(ring / dry)) <= 3.0);
}

/* -------------------------------------------------------------------------
 * Mid-steal handling (C5)
 * ---------------------------------------------------------------------- */

/* Put the voice mid-steal: playing 60 with a long release, stolen for 62 */
static void start_steal(void) {
  params[DCA_RELEASE].f = 0.5f;
  voice_update(&v);
  render_n(1);
  voice_trigger(&v, 60, 100);
  render_n(2);
  voice_trigger(&v, 62, 100);
  TEST_ASSERT_EQUAL(VOICE_STEALING, v.state);
  TEST_ASSERT_EQUAL(ENV_SHUTDOWN, v.amp_env.state);
}

static void render_until_active(void) {
  for (int i = 0; i < 16 && v.state != VOICE_ACTIVE; i++)
    render_n(1);
  TEST_ASSERT_EQUAL(VOICE_ACTIVE, v.state);
}

void note_off_during_steal_keeps_the_fade_and_defers_the_release(void) {
  start_steal();

  voice_gate_off(&v);

  TEST_ASSERT_EQUAL(ENV_SHUTDOWN, v.amp_env.state); /* fade untouched */
  TEST_ASSERT_FALSE(v.gate); /* applied when the new note starts */

  render_until_active();
  TEST_ASSERT_EQUAL_UINT8(62, v.note);
  TEST_ASSERT_EQUAL(ENV_RELEASE, v.amp_env.state);
}

void note_off_on_an_idle_voice_does_nothing(void) {
  TEST_ASSERT_EQUAL(VOICE_IDLE, v.state);
  voice_gate_off(&v);
  TEST_ASSERT_EQUAL(VOICE_IDLE, v.state);
  TEST_ASSERT_EQUAL(ENV_OFF, v.amp_env.state);
}

void retarget_steal_changes_the_incoming_note_and_cancels_release(void) {
  start_steal();
  voice_gate_off(&v); /* pending release for 62 */

  voice_trigger(&v, 64, 80);

  TEST_ASSERT_EQUAL(VOICE_STEALING, v.state);
  TEST_ASSERT_EQUAL(ENV_SHUTDOWN, v.amp_env.state);
  TEST_ASSERT_TRUE(v.gate); /* the new key is down */

  render_until_active();
  TEST_ASSERT_EQUAL_UINT8(64, v.note);
  TEST_ASSERT_EQUAL_UINT8(80, v.velocity);
  TEST_ASSERT_NOT_EQUAL(ENV_RELEASE, v.amp_env.state);
}

void legato_during_steal_retargets_and_leaves_the_fade_alone(void) {
  start_steal();

  voice_set_note(&v, 65);

  TEST_ASSERT_EQUAL(VOICE_STEALING, v.state);
  TEST_ASSERT_EQUAL(ENV_SHUTDOWN, v.amp_env.state);
  TEST_ASSERT_EQUAL_UINT8(60, v.note); /* still fading the old note */

  render_until_active();
  TEST_ASSERT_EQUAL_UINT8(65, v.note);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, MIDI_FREQ_TABLE[65], v.pitch);
}

/* A stolen voice's new note keeps the unison detune */
void stolen_note_keeps_unison_detune(void) {
  v.unison_cents = 10.0f;
  start_steal();
  render_until_active();

  TEST_ASSERT_FLOAT_WITHIN(1e-7f,
                           MIDI_FREQ_TABLE[62] * powf(2.0f, 10.0f / 1200.0f) /
                               AUDIO_SAMPLE_RATE,
                           v.dco_1.inc);
  v.unison_cents = 0.0f;
}

/*
 * C22: the start phase survives a steal - it is applied when the new note
 * starts, not to the voice while it fades the old one.  The new note starts
 * in the same block as the fade completes and then runs for that block.
 */
void start_phase_survives_a_steal(void) {
  start_steal();
  v.start_phase = 0.25f;
  render_until_active();

  float expected = 0.25f + (float)AUDIO_BLOCK_SIZE * v.dco_1.inc;
  expected -= floorf(expected);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, expected, v.dco_1.phase);
  v.start_phase = -1.0f;
}

int main(void) {
  UNITY_BEGIN();

  /* Initialisation / reset */
  RUN_TEST(init_sets_idle_state_and_clears_note_data);
  RUN_TEST(init_zeroes_the_sample_block);
  RUN_TEST(reset_returns_an_active_voice_to_idle);
  RUN_TEST(reset_preserves_a_pending_update);
  RUN_TEST(reset_pending_update_is_applied_on_next_render);
  RUN_TEST(init_clears_update_pending);

  /* Note on - free voice */
  RUN_TEST(note_on_from_idle_starts_immediately_and_moves_to_active);
  RUN_TEST(note_on_computes_pitch_from_midi_frequency_table);
  RUN_TEST(note_on_applies_unison_cents_to_pitch);
  RUN_TEST(unison_change_reaches_a_held_note);
  RUN_TEST(start_phase_is_applied_when_a_note_starts);
  RUN_TEST(note_on_hands_pitch_to_the_dcos_immediately);
  RUN_TEST(note_on_puts_envelope_into_attack_immediately);

  /* Note on - retrigger */
  RUN_TEST(retrigger_same_note_updates_velocity_immediately);
  RUN_TEST(retrigger_resets_envelope_back_to_attack);
  RUN_TEST(gate_follows_trigger_and_gate_off);

  /* Mid-steal handling (C5) */
  RUN_TEST(note_off_during_steal_keeps_the_fade_and_defers_the_release);
  RUN_TEST(note_off_on_an_idle_voice_does_nothing);
  RUN_TEST(retarget_steal_changes_the_incoming_note_and_cancels_release);
  RUN_TEST(legato_during_steal_retargets_and_leaves_the_fade_alone);
  RUN_TEST(stolen_note_keeps_unison_detune);
  RUN_TEST(start_phase_survives_a_steal);

  /* Note on - steal */
  RUN_TEST(note_on_different_note_while_active_starts_the_steal);
  RUN_TEST(
      steal_requested_before_the_stolen_notes_first_render_still_completes);
  RUN_TEST(trigger_while_stealing_retargets_the_steal);
  RUN_TEST(steal_completes_and_switches_to_the_new_note);
  RUN_TEST(steal_starts_the_new_note_in_the_same_block_the_rtz_completes);

  /* Note off / release */
  RUN_TEST(note_off_moves_envelope_to_release_immediately);
  RUN_TEST(note_off_release_uses_the_configured_release_time);
  RUN_TEST(voice_returns_to_idle_once_envelope_finishes);
  RUN_TEST(idle_voice_after_note_end_has_silent_sample_block);

  /* Legato */
  RUN_TEST(legato_changes_note_and_pitch_without_touching_envelope);
  RUN_TEST(legato_without_portamento_snaps_dco_pitch_immediately);
  RUN_TEST(legato_with_portamento_leaves_dco_pitch_for_the_glide_step);

  /* Update */
  RUN_TEST(update_while_idle_reloads_the_chain_without_changing_state);

  RUN_TEST(
      update_while_active_applies_new_params_without_interrupting_the_note);

  /* Mod wheel */
  RUN_TEST(mod_wheel_at_zero_leaves_every_destination_at_its_patched_depth);
  RUN_TEST(mod_wheel_vco_switch_boosts_dco_pitch_only);
  RUN_TEST(mod_wheel_vcf_switch_boosts_dcf_cutoff_only);
  RUN_TEST(mod_wheel_vco_vcf_switch_boosts_both);
  RUN_TEST(mod_wheel_never_reaches_dca_level_regardless_of_switch_position);
  RUN_TEST(mod_wheel_does_not_affect_pwm_destinations);

  /* Mod envelope (ENV1) */
  RUN_TEST(note_on_triggers_mod_env_into_attack_immediately);
  RUN_TEST(mod_env_level_reaches_env1_level_mod_source);
  RUN_TEST(note_off_releases_mod_env_same_as_amp_env);

  /* Cross-modulation (ring mod) */
  RUN_TEST(xmod_level_produces_an_audible_change_in_output);
  RUN_TEST(full_xmod_with_dco1_muted_removes_both_input_pitches);
  RUN_TEST(full_xmod_replaces_dco2_and_keeps_dco1);
  RUN_TEST(ring_level_follows_dco2_level_not_dco1_level);
  RUN_TEST(full_xmod_ring_is_about_as_loud_as_the_dco2_it_replaces);

  return UNITY_END();
}
