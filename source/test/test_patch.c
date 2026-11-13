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

#include "patch.h"
#include "unity.h"
#include <string.h>

static param_value_t params[PARAM_COUNT];

void setUp() { memset(params, 0, sizeof(params)); }

void tearDown() {}

/* -------------------------------------------------------------------------
 * Default patch
 * ---------------------------------------------------------------------- */

void default_patch_sets_synth_and_dca_baseline(void) {
  param_create_default_patch(params);

  TEST_ASSERT_EQUAL(VOICE_MODE_POLY, params[SYNTH_VOICE_MODE].e);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, params[DCA_LEVEL].f);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, params[DCA_ATTACK].f);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, params[DCA_SUSTAIN].f);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, params[DCA_RELEASE].f);
}

void default_patch_sets_dco1_and_dco2_with_distinct_mod_sources(void) {
  param_create_default_patch(params);

  TEST_ASSERT_EQUAL(DCO_SAW, params[DCO1_WAVE].e);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, params[DCO1_LEVEL].f);
  TEST_ASSERT_EQUAL_FLOAT(0.5f, params[DCO1_PULSEWIDTH].f);
  TEST_ASSERT_EQUAL_FLOAT(0.5f, params[DCO1_CENTS].f); /* in tune */
  TEST_ASSERT_EQUAL_FLOAT(0.5f, params[DCO2_CENTS].f);
  TEST_ASSERT_EQUAL(LFO1_SAW, params[DCO1_MOD_SOURCE].e);

  TEST_ASSERT_EQUAL(DCO_TRI, params[DCO2_WAVE].e);
  TEST_ASSERT_EQUAL(
      LFO1_RAMP,
      params[DCO2_MOD_SOURCE].e); /* deliberately different from DCO1 */
}

void default_patch_sets_filter_and_env_baseline(void) {
  param_create_default_patch(params);

  TEST_ASSERT_EQUAL(FILTER_LPF2, params[DCF_MODE].e);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, params[DCF_CUTOFF].f);
  TEST_ASSERT_FALSE(params[DCF_TRACK].b);

  TEST_ASSERT_EQUAL(ENV_NORMAL, params[ENV1_MODE].e);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, params[ENV1_SUSTAIN].f);
  TEST_ASSERT_EQUAL_FLOAT(0.5f, params[ENV1_RELEASE].f);

  TEST_ASSERT_EQUAL(LFO_MODE_NOTE, params[LFO1_MODE].e);
}

void default_patch_sets_global_controls_off(void) {
  param_create_default_patch(params);

  TEST_ASSERT_FALSE(params[SYNTH_PORTAMENTO_ON].b);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, params[SYNTH_PORTAMENTO_TIME].f);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, params[SYNTH_UNISON_DETUNE].f);
}

void default_patch_sets_bend_range_to_two_semitones(void) {
  param_create_default_patch(params);

  /* +/- 2 semitones, the MIDI default, normalised against BEND_RANGE_MAX */
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 2.0f / BEND_RANGE_MAX,
                           params[SYNTH_BEND_RANGE].f);
}

void default_patch_sets_mod_wheel_dest_to_vco(void) {
  param_create_default_patch(params);

  TEST_ASSERT_EQUAL(MOD_WHEEL_DEST_VCO, params[SYNTH_MOD_WHEEL_DEST].e);
}

void default_patch_sets_every_field_explicitly(void) {
  /* Fill with a non-zero, non-bool-like pattern first: there is no
     factory bank any more to lean on zero-initialised static storage,
     so param_create_default_patch() must set every field itself rather
     than relying on the caller's buffer already being zeroed (eg. a
     stack array, as synth.c's Program Change fallback uses). */
  for (int i = 0; i < PARAM_COUNT; i++) {
    params[i].i = 0x7A7A7A7A;
  }

  param_create_default_patch(params);

  TEST_ASSERT_FALSE(params[DCA_NOTE_TRACK].b);
  TEST_ASSERT_FALSE(params[DCA_VELOCITY_TRACK].b);
  TEST_ASSERT_FALSE(params[SYNTH_CHORUS_ON].b);
}

int main(void) {
  UNITY_BEGIN();

  /* Default patch */
  RUN_TEST(default_patch_sets_synth_and_dca_baseline);
  RUN_TEST(default_patch_sets_dco1_and_dco2_with_distinct_mod_sources);
  RUN_TEST(default_patch_sets_filter_and_env_baseline);
  RUN_TEST(default_patch_sets_global_controls_off);
  RUN_TEST(default_patch_sets_bend_range_to_two_semitones);
  RUN_TEST(default_patch_sets_mod_wheel_dest_to_vco);
  RUN_TEST(default_patch_sets_every_field_explicitly);

  return UNITY_END();
}
