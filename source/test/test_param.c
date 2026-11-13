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

#include "param.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* -------------------------------------------------------------------------
 * Parameter descriptor table
 * ---------------------------------------------------------------------- */

void param_desc_declares_expected_types_and_ranges(void) {
  TEST_ASSERT_EQUAL(PARAM_TYPE_FLOAT, param_desc[DCA_LEVEL].type);

  TEST_ASSERT_EQUAL(PARAM_TYPE_ENUM, param_desc[DCO1_WAVE].type);
  TEST_ASSERT_EQUAL_UINT32(DCO_WAVE_COUNT, param_desc[DCO1_WAVE].enum_count);

  TEST_ASSERT_EQUAL(PARAM_TYPE_INT, param_desc[DCO1_OCTAVE].type);
  TEST_ASSERT_EQUAL(-OSC_OCTAVE_RANGE, param_desc[DCO1_OCTAVE].min);
  TEST_ASSERT_EQUAL(OSC_OCTAVE_RANGE, param_desc[DCO1_OCTAVE].max);

  TEST_ASSERT_EQUAL(PARAM_TYPE_BOOL, param_desc[DCF_TRACK].type);
  TEST_ASSERT_EQUAL(PARAM_TYPE_BOOL, param_desc[DCA_NOTE_TRACK].type);
  TEST_ASSERT_EQUAL(PARAM_TYPE_BOOL, param_desc[DCA_VELOCITY_TRACK].type);
  TEST_ASSERT_EQUAL(PARAM_TYPE_FLOAT, param_desc[SYNTH_BEND_RANGE].type);

  TEST_ASSERT_EQUAL(PARAM_TYPE_ENUM, param_desc[SYNTH_MOD_WHEEL_DEST].type);
  TEST_ASSERT_EQUAL_UINT32(MOD_WHEEL_DEST_COUNT,
                           param_desc[SYNTH_MOD_WHEEL_DEST].enum_count);
}

/*
 * Every parameter must be described.  A missing table entry is zero-filled,
 * which is PARAM_TYPE_UNDEFINED - without this check it used to read as a
 * float, and the four DCO PWM parameters were missing unnoticed (the PWM
 * sources are enums, so UI and MIDI wrote float bits into an enum).
 */
void param_desc_describes_every_parameter(void) {
  for (int id = 0; id < PARAM_COUNT; id++) {
    if (id == VOICE_PARAM_COUNT) {
      continue; /* section marker, not a parameter */
    }

    TEST_ASSERT_NOT_EQUAL_MESSAGE(PARAM_TYPE_UNDEFINED, param_desc[id].type,
                                  "param_desc[] entry missing");

    if (param_desc[id].type == PARAM_TYPE_ENUM) {
      TEST_ASSERT_TRUE(param_desc[id].enum_count > 1);
    }

    if (param_desc[id].type == PARAM_TYPE_INT) {
      TEST_ASSERT_TRUE(param_desc[id].min < param_desc[id].max);
    }
  }
}

/* Each modulation source selector is an enum over the modulation sources */
void param_desc_mod_source_selectors_are_enums(void) {
  const enum param_id selectors[] = {DCA_MOD_SOURCE,  DCO1_MOD_SOURCE,
                                     DCO1_PWM_SOURCE, DCO2_MOD_SOURCE,
                                     DCO2_PWM_SOURCE, DCF_MOD_SOURCE};

  for (size_t i = 0; i < sizeof(selectors) / sizeof(selectors[0]); i++) {
    TEST_ASSERT_EQUAL(PARAM_TYPE_ENUM, param_desc[selectors[i]].type);
    TEST_ASSERT_EQUAL_UINT32(MOD_SOURCE_COUNT,
                             param_desc[selectors[i]].enum_count);
  }
}

/* -------------------------------------------------------------------------
 * Value-mapping helpers
 * ---------------------------------------------------------------------- */

void uni_to_linear_maps_endpoints_and_midpoint(void) {
  TEST_ASSERT_EQUAL_FLOAT(10.0f, param_uni_to_linear(0.0f, 10.0f, 20.0f));
  TEST_ASSERT_EQUAL_FLOAT(20.0f, param_uni_to_linear(1.0f, 10.0f, 20.0f));
  TEST_ASSERT_EQUAL_FLOAT(15.0f, param_uni_to_linear(0.5f, 10.0f, 20.0f));
}

void uni_to_exp_maps_endpoints(void) {
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 80.0f,
                           param_uni_to_exp(0.0f, 80.0f, 18000.0f));
  TEST_ASSERT_FLOAT_WITHIN(1e-1f, 18000.0f,
                           param_uni_to_exp(1.0f, 80.0f, 18000.0f));
}

void linear_to_uni_is_inverse_of_uni_to_linear(void) {
  float val = param_uni_to_linear(0.3f, 10.0f, 20.0f);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.3f, param_linear_to_uni(val, 10.0f, 20.0f));
}

void bipolar_to_uni_maps_endpoints_and_center(void) {
  TEST_ASSERT_EQUAL_FLOAT(0.0f, param_bipolar_to_uni(-1.0f, 1.0f));
  TEST_ASSERT_EQUAL_FLOAT(0.5f, param_bipolar_to_uni(0.0f, 1.0f));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, param_bipolar_to_uni(1.0f, 1.0f));
}

/* -------------------------------------------------------------------------
 * Parameter smoothing coefficient
 *
 * Shared by any module that smooths a block-rate parameter change (DCA,
 * DCF) rather than snapping it - see dca.c/dcf.c.
 * ---------------------------------------------------------------------- */

void smooth_coeff_is_a_valid_one_pole_coefficient(void) {
  /* A coefficient outside (0,1) would mean either no smoothing at all
     (0) or an instant snap every block (>=1). */
  float coeff = param_smooth_coeff(10.0f);

  TEST_ASSERT_TRUE(coeff > 0.0f);
  TEST_ASSERT_TRUE(coeff < 1.0f);
}

void smooth_coeff_is_stable_across_repeated_calls(void) {
  /* Every input is fixed at compile time, so the same time constant
     must keep returning the exact same (cached) value. */
  float first = param_smooth_coeff(10.0f);
  float second = param_smooth_coeff(10.0f);

  TEST_ASSERT_EQUAL_FLOAT(first, second);
}

void smooth_coeff_is_smaller_for_a_longer_time_constant(void) {
  /* A longer approach time means a smaller per-block step. */
  float fast = param_smooth_coeff(5.0f);
  float slow = param_smooth_coeff(50.0f);

  TEST_ASSERT_TRUE(slow < fast);
}

/*
 * C17: the cache holds PARAM_SMOOTH_COEFF_CACHE_SIZE time constants.  More
 * distinct values than that must still return correct coefficients, and must
 * not write past the cache (previously only an RTT_ASSERT guarded it, which
 * is compiled out of release builds).
 */
static float expected_coeff(float time_ms) {
  float blocks =
      fmaxf(time_ms * (AUDIO_SAMPLE_RATE / 1000.0f) / AUDIO_BLOCK_SIZE, 1.0f);
  return 1.0f - expf(-1.0f / blocks);
}

void smooth_coeff_is_correct_beyond_the_cache_size(void) {
  for (int i = 0; i < 3 * PARAM_SMOOTH_COEFF_CACHE_SIZE; i++) {
    float t = 100.0f + 7.0f * (float)i; /* all distinct */
    TEST_ASSERT_FLOAT_WITHIN(1e-7f, expected_coeff(t), param_smooth_coeff(t));
  }

  /* values cached earlier are still served correctly */
  TEST_ASSERT_FLOAT_WITHIN(1e-7f, expected_coeff(10.0f),
                           param_smooth_coeff(10.0f));
}

int main(void) {
  UNITY_BEGIN();

  /* Parameter descriptor table */
  RUN_TEST(param_desc_declares_expected_types_and_ranges);
  RUN_TEST(param_desc_describes_every_parameter);
  RUN_TEST(param_desc_mod_source_selectors_are_enums);

  /* Value-mapping helpers */
  RUN_TEST(uni_to_linear_maps_endpoints_and_midpoint);
  RUN_TEST(uni_to_exp_maps_endpoints);
  RUN_TEST(linear_to_uni_is_inverse_of_uni_to_linear);
  RUN_TEST(bipolar_to_uni_maps_endpoints_and_center);

  /* Parameter smoothing coefficient */
  RUN_TEST(smooth_coeff_is_a_valid_one_pole_coefficient);
  RUN_TEST(smooth_coeff_is_stable_across_repeated_calls);
  RUN_TEST(smooth_coeff_is_smaller_for_a_longer_time_constant);
  RUN_TEST(smooth_coeff_is_correct_beyond_the_cache_size);

  return UNITY_END();
}
