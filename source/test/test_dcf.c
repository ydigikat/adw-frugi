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

#include "dcf.h"
#include "unity.h"
#include <math.h>

static struct dcf dcf;
static float block[AUDIO_BLOCK_SIZE];

void setUp(void) {
  dcf_init(&dcf);
  for (uint16_t i = 0; i < AUDIO_BLOCK_SIZE; i++)
    block[i] = 0.0f;
}

void tearDown(void) {}

/* -------------------------------------------------------------------------
 * Initialisation
 * ---------------------------------------------------------------------- */

void init_sets_unity_coefficients(void) {
  TEST_ASSERT_EQUAL_FLOAT(1.0f, dcf.alpha0);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, dcf.f1.z1);
}

/* -------------------------------------------------------------------------
 * Update mapping
 * ---------------------------------------------------------------------- */

void update_stores_mode_and_tracking_flag(void) {
  dcf_update(&dcf, FILTER_HPF4, 0.5f, 0.0f, 0.0f, true);

  TEST_ASSERT_EQUAL(FILTER_HPF4, dcf.filter_mode_param);
  TEST_ASSERT_TRUE(dcf.note_tracking_param);
}

void note_on_stores_note(void) {
  dcf_note_on(&dcf, 72);
  TEST_ASSERT_EQUAL_UINT8(72, dcf.note);
}

/* -------------------------------------------------------------------------
 * Key tracking
 *
 * dcf_calculate_coefficients() (run at the top of dcf_render()) applies
 * full (1 octave/octave) keytrack relative to middle C (note 60) when
 * note_tracking_param is set. The private FILTER_CUT_MIN/MAX constants
 * aren't visible here, so these tests compare cutoff *ratios* across
 * renders rather than asserting an absolute cutoff value.
 * ---------------------------------------------------------------------- */

void note_tracking_doubles_cutoff_one_octave_above_reference(void) {
  dcf_update(&dcf, FILTER_LPF2, 0.3f, 0.0f, 0.0f, true);

  dcf_note_on(&dcf, 60); /* reference note - keytrack multiplier is 1.0 */
  dcf_render(&dcf, block, 0.0f);
  float cutoff_ref = dcf.cutoff;

  dcf_note_on(&dcf, 72); /* +1 octave */
  dcf_render(&dcf, block, 0.0f);
  float cutoff_octave_up = dcf.cutoff;

  TEST_ASSERT_FLOAT_WITHIN(1.0f, cutoff_ref * 2.0f, cutoff_octave_up);
}

void note_tracking_halves_cutoff_one_octave_below_reference(void) {
  dcf_update(&dcf, FILTER_LPF2, 0.3f, 0.0f, 0.0f, true);

  dcf_note_on(&dcf, 60);
  dcf_render(&dcf, block, 0.0f);
  float cutoff_ref = dcf.cutoff;

  dcf_note_on(&dcf, 48); /* -1 octave */
  dcf_render(&dcf, block, 0.0f);
  float cutoff_octave_down = dcf.cutoff;

  TEST_ASSERT_FLOAT_WITHIN(1.0f, cutoff_ref * 0.5f, cutoff_octave_down);
}

void note_tracking_disabled_leaves_cutoff_unaffected_by_note(void) {
  dcf_update(&dcf, FILTER_LPF2, 0.3f, 0.0f, 0.0f, false);

  dcf_note_on(&dcf, 60);
  dcf_render(&dcf, block, 0.0f);
  float cutoff_low_note = dcf.cutoff;

  dcf_note_on(&dcf, 96);
  dcf_render(&dcf, block, 0.0f);
  float cutoff_high_note = dcf.cutoff;

  TEST_ASSERT_EQUAL_FLOAT(cutoff_low_note, cutoff_high_note);
}

void note_tracking_stays_within_filter_range_at_extreme_notes(void) {
  dcf_update(&dcf, FILTER_LPF2, 1.0f, 0.0f, 0.0f,
             true); /* cutoff already maxed */

  dcf_note_on(&dcf,
              127); /* would blow far past FILTER_CUT_MAX without the clamp */
  dcf_render(&dcf, block, 0.0f);

  /* Clamped to the top of the range */
  TEST_ASSERT_EQUAL_FLOAT(DCF_CUTOFF_MAX_HZ, dcf.cutoff);
}

/* -------------------------------------------------------------------------
 * Range clamping (C7)
 *
 * A cutoff driven outside [DCF_CUTOFF_MIN_HZ, DCF_CUTOFF_MAX_HZ] by
 * modulation or key tracking clamps to the nearer limit.  Previously anything
 * below the minimum jumped to the maximum, so a downward sweep snapped the
 * filter fully open.
 * ---------------------------------------------------------------------- */

void low_note_tracking_clamps_at_the_minimum(void) {
  dcf_update(&dcf, FILTER_LPF2, 0.0f, 0.0f, 0.0f, true); /* at the minimum */
  dcf_note_on(&dcf, 0); /* five octaves below the reference note */
  dcf_render(&dcf, block, 0.0f);

  TEST_ASSERT_EQUAL_FLOAT(DCF_CUTOFF_MIN_HZ, dcf.cutoff);
}

void negative_modulation_clamps_at_the_minimum(void) {
  dcf_update(&dcf, FILTER_LPF4, 0.1f, 0.0f, 0.0f, false);
  dcf_note_on(&dcf, 60);
  dcf_render(&dcf, block, -1.0f); /* drives the cutoff below the minimum */

  TEST_ASSERT_EQUAL_FLOAT(DCF_CUTOFF_MIN_HZ, dcf.cutoff);
}

void positive_modulation_clamps_at_the_maximum(void) {
  dcf_update(&dcf, FILTER_LPF4, 0.9f, 0.0f, 0.0f, false);
  dcf_note_on(&dcf, 60);
  dcf_render(&dcf, block, 1.0f); /* drives the cutoff above the maximum */

  TEST_ASSERT_EQUAL_FLOAT(DCF_CUTOFF_MAX_HZ, dcf.cutoff);
}

/* A downward sweep must close the filter steadily, never re-open it */
void downward_modulation_sweep_never_reopens_the_filter(void) {
  dcf_update(&dcf, FILTER_LPF4, 0.5f, 0.0f, 0.0f, false);
  dcf_note_on(&dcf, 60);

  float previous = DCF_CUTOFF_MAX_HZ + 1.0f;

  for (int step = 0; step <= 40; step++) {
    float mod = -2.0f * (float)step / 40.0f; /* 0 down to -2 */
    dcf_render(&dcf, block, mod);

    TEST_ASSERT_TRUE(dcf.cutoff <= previous);
    TEST_ASSERT_TRUE(dcf.cutoff >= DCF_CUTOFF_MIN_HZ);
    previous = dcf.cutoff;
  }

  TEST_ASSERT_EQUAL_FLOAT(DCF_CUTOFF_MIN_HZ, previous);
}

/* In-range values are left alone */
void in_range_cutoff_is_not_clamped(void) {
  dcf_update(&dcf, FILTER_LPF2, 0.5f, 0.0f, 0.0f, false);
  dcf_note_on(&dcf, 60);
  dcf_render(&dcf, block, 0.0f);

  TEST_ASSERT_TRUE(dcf.cutoff > DCF_CUTOFF_MIN_HZ);
  TEST_ASSERT_TRUE(dcf.cutoff < DCF_CUTOFF_MAX_HZ);
  /* exponential mapping: midpoint is the geometric mean of the range */
  TEST_ASSERT_FLOAT_WITHIN(
      1.0f, sqrtf(DCF_CUTOFF_MIN_HZ * DCF_CUTOFF_MAX_HZ), dcf.cutoff);
}

/* -------------------------------------------------------------------------
 * Parameter smoothing
 *
 * dcf_note_on() snaps cutoff_param straight to whatever was last set by
 * dcf_update() - that's why every test above can update() then note_on()
 * then render() in one go and see the new value immediately. A cutoff
 * change with no note_on (a CC/knob sweep while a note is already
 * sounding) must instead approach the target gradually - see Part 2,
 * "Parameter smoothing".
 * ---------------------------------------------------------------------- */

void init_computes_smoothing_coefficient_in_valid_range(void) {
  TEST_ASSERT_TRUE(dcf.smooth_coeff > 0.0f);
  TEST_ASSERT_TRUE(dcf.smooth_coeff < 1.0f);
}

void update_without_note_on_moves_cutoff_gradually(void) {
  dcf_update(&dcf, FILTER_LPF2, 0.0f, 0.0f, 0.0f, false);
  dcf_note_on(&dcf, 60);
  dcf_render(&dcf, block, 0.0f); /* settle at the low-cutoff target */
  float cutoff_before = dcf.cutoff;

  dcf_update(&dcf, FILTER_LPF2, 1.0f, 0.0f, 0.0f,
             false); /* sweep cutoff open, no note_on */
  dcf_render(&dcf, block, 0.0f);
  float cutoff_one_block = dcf.cutoff;

  for (int i = 0; i < 200; i++)
    dcf_render(&dcf, block, 0.0f);
  float cutoff_settled = dcf.cutoff;

  /* One block in, we should be past the start but nowhere near the fully
     settled value yet - a straight jump would put cutoff_one_block right
     next to cutoff_settled instead. */
  TEST_ASSERT_TRUE(cutoff_one_block > cutoff_before);
  TEST_ASSERT_TRUE(cutoff_one_block < cutoff_settled);
}

void note_on_bypasses_smoothing_and_snaps_immediately(void) {
  dcf_update(&dcf, FILTER_LPF2, 0.0f, 0.0f, 0.0f, false);
  dcf_note_on(&dcf, 60);
  dcf_render(&dcf, block, 0.0f);

  dcf_update(&dcf, FILTER_LPF2, 1.0f, 0.0f, 0.0f, false);
  for (int i = 0; i < 200; i++)
    dcf_render(&dcf, block, 0.0f);
  float cutoff_after_slow_sweep = dcf.cutoff;

  /* A fresh note at the same target should land on the same cutoff in a
     single render, not need those 200 blocks to get there. */
  dcf_note_on(&dcf, 60);
  dcf_render(&dcf, block, 0.0f);

  TEST_ASSERT_FLOAT_WITHIN(1.0f, cutoff_after_slow_sweep, dcf.cutoff);
}

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(init_sets_unity_coefficients);

  RUN_TEST(update_stores_mode_and_tracking_flag);
  RUN_TEST(note_on_stores_note);

  RUN_TEST(note_tracking_doubles_cutoff_one_octave_above_reference);
  RUN_TEST(note_tracking_halves_cutoff_one_octave_below_reference);
  RUN_TEST(note_tracking_disabled_leaves_cutoff_unaffected_by_note);
  RUN_TEST(note_tracking_stays_within_filter_range_at_extreme_notes);

  /* Range clamping (C7) */
  RUN_TEST(low_note_tracking_clamps_at_the_minimum);
  RUN_TEST(negative_modulation_clamps_at_the_minimum);
  RUN_TEST(positive_modulation_clamps_at_the_maximum);
  RUN_TEST(downward_modulation_sweep_never_reopens_the_filter);
  RUN_TEST(in_range_cutoff_is_not_clamped);

  RUN_TEST(init_computes_smoothing_coefficient_in_valid_range);
  RUN_TEST(update_without_note_on_moves_cutoff_gradually);
  RUN_TEST(note_on_bypasses_smoothing_and_snaps_immediately);

  return UNITY_END();
}
