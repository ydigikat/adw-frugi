/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   A.I. Disclosure:  These unit tests written by Anthropic Claude.
   -----------------------------------------------------------------------------
*/

#include <math.h>

#include "env.h"
#include "unity.h"

static struct env env;

/*
 * Drives the envelope with successive renders until it reaches
 * `target` state or `max_iters` is exceeded (test failure).
 */
static void advance_until(enum env_state target, int max_iters) {
  float out;
  for (int i = 0; i < max_iters; i++) {
    env_render(&env, &out);
    if (env.state == target)
      return;
  }
  TEST_FAIL_MESSAGE("envelope did not reach target state in time");
}

void setUp() { env_init(&env); }

void tearDown() {}

/* -------------------------------------------------------------------------
 * Initialisation / reset
 * ---------------------------------------------------------------------- */

void init_sets_off_state_and_zero_level(void) {
  TEST_ASSERT_EQUAL(ENV_OFF, env.state);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, env.level);
}

void reset_clears_state_and_level(void) {
  env.state = ENV_SUSTAIN;
  env.level = 0.75f;

  env_reset(&env);

  TEST_ASSERT_EQUAL(ENV_OFF, env.state);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, env.level);
}

void off_state_forces_stale_level_to_zero(void) {
  /* Simulates stale level data lingering in OFF state (e.g. after a
     direct state assignment elsewhere) - OFF must always render silence. */
  env.state = ENV_OFF;
  env.level = 0.9f;

  float out;
  env_render(&env, &out);

  TEST_ASSERT_EQUAL_FLOAT(0.0f, env.level);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, out);
}

/* -------------------------------------------------------------------------
 * Parameter update mapping
 * ---------------------------------------------------------------------- */

void update_maps_attack_time_linearly(void) {
  env_update(&env, 0.0f, 0.0f, 0.0f, 0.0f, ENV_NORMAL, false, false);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, env.attack);

  env_update(&env, 0.5f, 0.0f, 0.0f, 0.0f, ENV_NORMAL, false, false);
  TEST_ASSERT_EQUAL_FLOAT(5000.0f, env.attack);

  env_update(&env, 1.0f, 0.0f, 0.0f, 0.0f, ENV_NORMAL, false, false);
  TEST_ASSERT_EQUAL_FLOAT(10000.0f, env.attack);
}

void update_maps_decay_time_linearly(void) {
  env_update(&env, 0.0f, 0.25f, 0.0f, 0.0f, ENV_NORMAL, false, false);
  TEST_ASSERT_EQUAL_FLOAT(2500.0f, env.decay);
}

void update_maps_release_time_linearly(void) {
  env_update(&env, 0.0f, 0.0f, 0.0f, 0.75f, ENV_NORMAL, false, false);
  TEST_ASSERT_EQUAL_FLOAT(7500.0f, env.release);
}

void update_stores_sustain_directly(void) {
  /* Sustain is a level, not a time - passed through unscaled */
  env_update(&env, 0.0f, 0.0f, 0.42f, 0.0f, ENV_NORMAL, false, false);
  TEST_ASSERT_EQUAL_FLOAT(0.42f, env.sustain);
}

void update_stores_mode_and_tracking_flags(void) {
  env_update(&env, 0.0f, 0.0f, 0.0f, 0.0f, ENV_INVERTED, true, true);
  TEST_ASSERT_EQUAL(ENV_INVERTED, env.mode);
  TEST_ASSERT_TRUE(env.note_track);
  TEST_ASSERT_TRUE(env.velocity_track);
}

/* -------------------------------------------------------------------------
 * Note on / attack
 * ---------------------------------------------------------------------- */

void note_on_transitions_to_attack_state(void) {
  env_note_on(&env, 60, 100);
  TEST_ASSERT_EQUAL(ENV_ATTACK, env.state);
}

void zero_attack_time_jumps_immediately_to_full_level_and_decay(void) {
  env_update(&env, 0.0f, 0.0f, 0.5f, 0.0f, ENV_NORMAL, false, false);
  env_note_on(&env, 60, 100);

  float out;
  env_render(&env, &out);

  TEST_ASSERT_EQUAL(ENV_DECAY, env.state);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, env.level);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, out);
}

void nonzero_attack_rises_monotonically_to_full_level(void) {
  env_update(&env, 0.01f, 0.0f, 0.5f, 0.0f, ENV_NORMAL, false,
             false); /* 100ms attack */
  env_note_on(&env, 60, 100);

  float out;
  float previous_level = env.level;
  int iterations = 0;

  while (env.state == ENV_ATTACK && iterations < 200) {
    env_render(&env, &out);
    TEST_ASSERT_TRUE(env.level >= previous_level);
    previous_level = env.level;
    iterations++;
  }

  TEST_ASSERT_EQUAL(ENV_DECAY, env.state);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, env.level);
}

/* -------------------------------------------------------------------------
 * Note / velocity tracking
 * ---------------------------------------------------------------------- */

void note_on_sets_attack_scaler_from_velocity_when_tracking_enabled(void) {
  env_update(&env, 0.5f, 0.0f, 0.5f, 0.0f, ENV_NORMAL, false,
             true); /* velocity_track = true */
  env_note_on(&env, 60, 127);

  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f - 127.0f / 127.0f, env.attack_scaler);
}

void note_on_leaves_attack_scaler_at_one_when_tracking_disabled(void) {
  env_update(&env, 0.5f, 0.0f, 0.5f, 0.0f, ENV_NORMAL, false, false);
  env_note_on(&env, 60, 127);

  TEST_ASSERT_EQUAL_FLOAT(1.0f, env.attack_scaler);
}

void note_on_sets_decay_scaler_from_note_when_tracking_enabled(void) {
  env_update(&env, 0.0f, 0.5f, 0.5f, 0.0f, ENV_NORMAL, true,
             false); /* note_track = true */
  env_note_on(&env, 127, 100);

  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f - 127.0f / 127.0f, env.decay_scaler);
}

void note_on_leaves_decay_scaler_at_one_when_tracking_disabled(void) {
  env_update(&env, 0.0f, 0.5f, 0.5f, 0.0f, ENV_NORMAL, false, false);
  env_note_on(&env, 127, 100);

  TEST_ASSERT_EQUAL_FLOAT(1.0f, env.decay_scaler);
}

void max_velocity_reaches_full_level_faster_when_velocity_tracked(void) {
  env_update(&env, 0.5f, 0.0f, 0.5f, 0.0f, ENV_NORMAL, false,
             true); /* ~5000ms attack, tracked */
  env_note_on(
      &env, 60,
      127); /* max velocity -> scaler 0, floored to 1 block -> near-instant */

  float out;
  int iterations = 0;
  while (env.state == ENV_ATTACK && iterations < 300) {
    env_render(&env, &out);
    iterations++;
  }

  /* A ~5s untracked attack takes thousands of 64-sample blocks at 44.1kHz;
     tracked to near-instant it should complete within a handful. */
  TEST_ASSERT_LESS_THAN_INT(5, iterations);
}

void max_note_reaches_sustain_faster_when_note_tracked(void) {
  env_update(&env, 0.0f, 0.5f, 0.5f, 0.0f, ENV_NORMAL, true,
             false); /* ~5000ms decay, tracked */
  env_note_on(
      &env, 127,
      100); /* max note -> scaler 0, floored to 1 block -> near-instant */

  float out;
  env_render(&env, &out); /* zero attack -> immediately in DECAY */
  TEST_ASSERT_EQUAL(ENV_DECAY, env.state);

  int iterations = 0;
  while (env.state == ENV_DECAY && iterations < 300) {
    env_render(&env, &out);
    iterations++;
  }

  TEST_ASSERT_LESS_THAN_INT(5, iterations);
}

/* -------------------------------------------------------------------------
 * Decay
 * ---------------------------------------------------------------------- */

void zero_decay_time_jumps_to_sustain_when_sustain_positive(void) {
  env_update(&env, 0.0f, 0.0f, 0.5f, 0.0f, ENV_NORMAL, false, false);
  env_note_on(&env, 60, 100);

  float out;
  env_render(&env, &out); /* attack -> decay, level = 1.0 */
  env_render(&env, &out); /* decay -> sustain, level = sustain */

  TEST_ASSERT_EQUAL(ENV_SUSTAIN, env.state);
  TEST_ASSERT_EQUAL_FLOAT(0.5f, env.level);
}

void zero_decay_time_jumps_to_release_when_sustain_zero(void) {
  env_update(&env, 0.0f, 0.0f, 0.0f, 0.0f, ENV_NORMAL, false, false);
  env_note_on(&env, 60, 100);

  float out;
  env_render(&env, &out); /* attack -> decay, level = 1.0 */
  env_render(&env, &out); /* decay -> release, level = 0.0 (sustain) */

  TEST_ASSERT_EQUAL(ENV_RELEASE, env.state);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, env.level);
}

void nonzero_decay_falls_to_sustain_level(void) {
  env_update(&env, 0.0f, 0.01f, 0.5f, 0.0f, ENV_NORMAL, false,
             false); /* 100ms decay */
  env_note_on(&env, 60, 100);

  float out;
  env_render(&env, &out); /* attack -> decay, level = 1.0 */

  float previous_level = env.level;
  int iterations = 0;
  while (env.state == ENV_DECAY && iterations < 300) {
    env_render(&env, &out);
    TEST_ASSERT_TRUE(env.level <= previous_level);
    previous_level = env.level;
    iterations++;
  }

  TEST_ASSERT_EQUAL(ENV_SUSTAIN, env.state);
  TEST_ASSERT_EQUAL_FLOAT(0.5f, env.level);
}

/* -------------------------------------------------------------------------
 * Sustain
 * ---------------------------------------------------------------------- */

void sustain_holds_level_across_multiple_renders(void) {
  env_update(&env, 0.0f, 0.0f, 0.5f, 0.0f, ENV_NORMAL, false, false);
  env_note_on(&env, 60, 100);
  advance_until(ENV_SUSTAIN, 10);

  float out;
  for (int i = 0; i < 5; i++) {
    env_render(&env, &out);
    TEST_ASSERT_EQUAL(ENV_SUSTAIN, env.state);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, env.level);
  }
}

void sustain_tracks_a_mid_hold_sustain_change(void) {
  env_update(&env, 0.0f, 0.0f, 0.5f, 0.0f, ENV_NORMAL, false, false);
  env_note_on(&env, 60, 100);
  advance_until(ENV_SUSTAIN, 10);

  env_update(&env, 0.0f, 0.0f, 0.2f, 0.0f, ENV_NORMAL, false, false);

  float out;
  env_render(&env, &out);

  TEST_ASSERT_EQUAL(ENV_SUSTAIN, env.state);
  TEST_ASSERT_EQUAL_FLOAT(0.2f, env.level);
  TEST_ASSERT_EQUAL_FLOAT(0.2f, out);
}

/* -------------------------------------------------------------------------
 * Note off / release
 * ---------------------------------------------------------------------- */

void note_off_from_positive_level_enters_release(void) {
  env_update(&env, 0.0f, 0.0f, 0.5f, 0.0f, ENV_NORMAL, false, false);
  env_note_on(&env, 60, 100);
  advance_until(ENV_SUSTAIN, 10);

  env_note_off(&env);

  TEST_ASSERT_EQUAL(ENV_RELEASE, env.state);
}

void note_off_at_zero_level_stays_off(void) {
  /* Envelope starts OFF with level 0.0 from setUp() */
  env_note_off(&env);

  TEST_ASSERT_EQUAL(ENV_OFF, env.state);
}

void zero_release_time_snaps_to_off_immediately(void) {
  env_update(&env, 0.0f, 0.0f, 0.5f, 0.0f, ENV_NORMAL, false, false);
  env_note_on(&env, 60, 100);
  advance_until(ENV_SUSTAIN, 10);

  env_note_off(&env);

  float out;
  env_render(&env, &out);

  TEST_ASSERT_EQUAL(ENV_OFF, env.state);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, env.level);
}

void nonzero_release_falls_to_zero_then_off(void) {
  env_update(&env, 0.0f, 0.0f, 0.5f, 0.01f, ENV_NORMAL, false,
             false); /* 100ms release */
  env_note_on(&env, 60, 100);
  advance_until(ENV_SUSTAIN, 10);

  env_note_off(&env);

  float out;
  float previous_level = env.level;
  int iterations = 0;
  while (env.state == ENV_RELEASE && iterations < 300) {
    env_render(&env, &out);
    TEST_ASSERT_TRUE(env.level <= previous_level);
    previous_level = env.level;
    iterations++;
  }

  TEST_ASSERT_EQUAL(ENV_OFF, env.state);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, env.level);
}

/* -------------------------------------------------------------------------
 * Return to zero (voice stealing)
 * ---------------------------------------------------------------------- */

void rtz_from_active_level_reaches_zero_and_off(void) {
  env_update(&env, 0.0f, 0.0f, 0.5f, 0.0f, ENV_NORMAL, false, false);
  env_note_on(&env, 60, 100);
  advance_until(ENV_SUSTAIN, 10);

  env_rtz(&env);
  TEST_ASSERT_EQUAL(ENV_SHUTDOWN, env.state);

  advance_until(ENV_OFF, 10);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, env.level);
}

void rtz_fades_over_several_blocks_rather_than_an_instant_cut(void) {
  /* Regression guard: RTZ must be a short linear fade, not a single-block
     jump straight to zero - a hard one-block cut is as audible a
     discontinuity as the click RTZ exists to avoid in the first place. */
  env_update(&env, 0.0f, 0.0f, 0.5f, 0.0f, ENV_NORMAL, false, false);
  env_note_on(&env, 60, 100);
  advance_until(ENV_SUSTAIN, 10);

  env_rtz(&env);
  float out;
  env_render(&env, &out);

  TEST_ASSERT_EQUAL(ENV_SHUTDOWN, env.state);
  TEST_ASSERT_TRUE(env.level > 0.0f);
}

void rtz_is_noop_when_level_already_zero(void) {
  /* Envelope starts OFF with level 0.0 from setUp() */
  env_rtz(&env);

  TEST_ASSERT_EQUAL(ENV_OFF, env.state);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, env.level);
}

void rtz_forces_off_when_level_is_zero_but_state_is_not(void) {
  /* env_note_on() moves state to ATTACK without touching level - if it
     hasn't been rendered even once yet, level is still 0.0. A caller (a
     voice steal request arriving before the note's first render) can hit
     this exact state. Since RTZ's contract is "silence regardless of
     current state", this must still land on ENV_OFF - not leave the
     envelope stuck running its ATTACK/DECAY/SUSTAIN segments with nothing
     ever setting it back to OFF. */
  env_note_on(&env, 60, 100);
  TEST_ASSERT_EQUAL(ENV_ATTACK, env.state);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, env.level);

  env_rtz(&env);

  TEST_ASSERT_EQUAL(ENV_OFF, env.state);
}

/* -------------------------------------------------------------------------
 * Output transforms (mode)
 *
 * Each test drives attack/decay to zero so the envelope settles at
 * level == sustain in a single pair of renders, isolating the mode
 * transform applied on top of that level.
 * ---------------------------------------------------------------------- */

void normal_mode_returns_level(void) {
  env_update(&env, 0.0f, 0.0f, 0.3f, 0.0f, ENV_NORMAL, false, false);
  env_note_on(&env, 60, 100);

  float out;
  env_render(&env, &out);
  env_render(&env, &out);

  TEST_ASSERT_EQUAL_FLOAT(0.3f, out);
}

void biased_mode_returns_level_minus_sustain(void) {
  env_update(&env, 0.0f, 0.0f, 0.3f, 0.0f, ENV_BIASED, false, false);
  env_note_on(&env, 60, 100);

  float out;
  env_render(&env, &out);
  env_render(&env, &out);

  TEST_ASSERT_EQUAL_FLOAT(0.0f, out); /* level == sustain here, so bias is 0 */
}

void inverted_mode_returns_one_minus_level(void) {
  env_update(&env, 0.0f, 0.0f, 0.3f, 0.0f, ENV_INVERTED, false, false);
  env_note_on(&env, 60, 100);

  float out;
  env_render(&env, &out);
  env_render(&env, &out);

  TEST_ASSERT_EQUAL_FLOAT(0.7f, out);
}

void inverted_biased_mode_returns_inverted_minus_sustain(void) {
  env_update(&env, 0.0f, 0.0f, 0.3f, 0.0f, ENV_INVERTED_BIASED, false, false);
  env_note_on(&env, 60, 100);

  float out;
  env_render(&env, &out);
  env_render(&env, &out);

  TEST_ASSERT_EQUAL_FLOAT(0.4f, out);
}

/* -------------------------------------------------------------------------
 * Zero release time (C15)
 *
 * Attack and decay floor their segment length at one block before dividing
 * by it; release did not, so a zero release divided by zero.  The release
 * state short-circuits a zero release, so the coefficient is never used, but
 * release builds use -ffast-math, which lets the compiler assume no division
 * by zero or infinity ever happens.  The coefficient must be the same finite
 * one-block value attack and decay use.
 * ---------------------------------------------------------------------- */

void zero_release_gives_the_finite_one_block_coefficient(void) {
  env_update(&env, 0.0f, 0.0f, 1.0f, 0.0f, ENV_NORMAL, false, false);

  const float one_block =
      env.release_tco / (1.0f + env.release_tco); /* exp(-ln((1+t)/t)) */

  TEST_ASSERT_TRUE(isfinite(env.release_coeff));
  TEST_ASSERT_TRUE(isfinite(env.release_overshoot));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, one_block, env.release_coeff);
}

/* ...and a zero release still ends the note at once */
void zero_release_still_ends_the_note_immediately(void) {
  float out;
  env_update(&env, 0.0f, 0.0f, 1.0f, 0.0f, ENV_NORMAL, false, false);
  env_note_on(&env, 60, 100);
  env_render(&env, &out);
  env_render(&env, &out);

  env_note_off(&env);
  env_render(&env, &out);

  TEST_ASSERT_EQUAL(ENV_OFF, env.state);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, out);
}

int main(void) {
  UNITY_BEGIN();

  /* Initialisation / reset */
  RUN_TEST(init_sets_off_state_and_zero_level);
  RUN_TEST(reset_clears_state_and_level);
  RUN_TEST(off_state_forces_stale_level_to_zero);

  /* Parameter update mapping */
  RUN_TEST(update_maps_attack_time_linearly);
  RUN_TEST(update_maps_decay_time_linearly);
  RUN_TEST(update_maps_release_time_linearly);
  RUN_TEST(update_stores_sustain_directly);
  RUN_TEST(update_stores_mode_and_tracking_flags);

  /* Note on / attack */
  RUN_TEST(note_on_transitions_to_attack_state);
  RUN_TEST(zero_attack_time_jumps_immediately_to_full_level_and_decay);
  RUN_TEST(nonzero_attack_rises_monotonically_to_full_level);

  /* Note / velocity tracking */
  RUN_TEST(note_on_sets_attack_scaler_from_velocity_when_tracking_enabled);
  RUN_TEST(note_on_leaves_attack_scaler_at_one_when_tracking_disabled);
  RUN_TEST(note_on_sets_decay_scaler_from_note_when_tracking_enabled);
  RUN_TEST(note_on_leaves_decay_scaler_at_one_when_tracking_disabled);
  RUN_TEST(max_velocity_reaches_full_level_faster_when_velocity_tracked);
  RUN_TEST(max_note_reaches_sustain_faster_when_note_tracked);

  /* Decay */
  RUN_TEST(zero_decay_time_jumps_to_sustain_when_sustain_positive);
  RUN_TEST(zero_decay_time_jumps_to_release_when_sustain_zero);
  RUN_TEST(nonzero_decay_falls_to_sustain_level);

  /* Sustain */
  RUN_TEST(sustain_holds_level_across_multiple_renders);
  RUN_TEST(sustain_tracks_a_mid_hold_sustain_change);

  /* Note off / release */
  RUN_TEST(note_off_from_positive_level_enters_release);
  RUN_TEST(note_off_at_zero_level_stays_off);
  RUN_TEST(zero_release_time_snaps_to_off_immediately);
  RUN_TEST(nonzero_release_falls_to_zero_then_off);

  /* Return to zero */
  RUN_TEST(rtz_from_active_level_reaches_zero_and_off);
  RUN_TEST(rtz_fades_over_several_blocks_rather_than_an_instant_cut);
  RUN_TEST(rtz_is_noop_when_level_already_zero);
  RUN_TEST(rtz_forces_off_when_level_is_zero_but_state_is_not);

  /* Output transforms */
  RUN_TEST(normal_mode_returns_level);
  RUN_TEST(biased_mode_returns_level_minus_sustain);
  RUN_TEST(inverted_mode_returns_one_minus_level);
  RUN_TEST(inverted_biased_mode_returns_inverted_minus_sustain);

  /* Zero release time (C15) */
  RUN_TEST(zero_release_gives_the_finite_one_block_coefficient);
  RUN_TEST(zero_release_still_ends_the_note_immediately);

  return UNITY_END();
}
