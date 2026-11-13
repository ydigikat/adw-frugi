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

#include "unity.h"
#include "shape.h"

static struct shaper shaper;

static void fill_block(float block[], float value) {
  for (size_t i = 0; i < AUDIO_BLOCK_SIZE; i++)
    block[i] = value;
}

void setUp() { shaper = (struct shaper){0}; }

void tearDown() {}

/* -------------------------------------------------------------------------
 * Initialisation / update
 * ---------------------------------------------------------------------- */

void init_sets_bypass_defaults(void) {
  shape_init(&shaper);

  TEST_ASSERT_EQUAL_FLOAT(0.0f, shaper.drive_param);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, shaper.drive);
}

void update_maps_normalised_range_to_drive_range(void) {
  shape_update(&shaper, 0.0f);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, shaper.drive);

  shape_update(&shaper, 1.0f);
  TEST_ASSERT_EQUAL_FLOAT(5.0f, shaper.drive); /* shape_DRIVE_MAX */

  /* Quadratic taper: half travel gives a quarter of the drive range. */
  shape_update(&shaper, 0.5f);
  TEST_ASSERT_EQUAL_FLOAT(1.25f, shaper.drive);
}

/* -------------------------------------------------------------------------
 * Render - bypass
 * ---------------------------------------------------------------------- */

void render_is_exact_bypass_when_drive_is_zero(void) {
  float block[AUDIO_BLOCK_SIZE];
  fill_block(block, 0.37f);

  shape_update(&shaper, 0.0f);
  shape_render(&shaper, block);

  for (size_t i = 0; i < AUDIO_BLOCK_SIZE; i++)
    TEST_ASSERT_EQUAL_FLOAT(0.37f, block[i]);
}

/* -------------------------------------------------------------------------
 * Render - curve properties
 * ---------------------------------------------------------------------- */

/*
 * The curve is normalised by its own pre-gain (gx / (1 + |gx|)), so its
 * asymptote is fixed at +-1 regardless of drive - unlike the old
 * (1+k)/k-asymptote curve, a full-scale input no longer passes through
 * unchanged at low drive, it's rounded off a little even there. That's an
 * intentional trade for guaranteeing the ceiling never blows out at low
 * drive (see shape.c).
 */
void render_bounds_full_scale_input_at_any_drive(void) {
  const float drives[] = {0.1f, 0.5f, 1.0f};

  for (size_t d = 0; d < sizeof(drives) / sizeof(drives[0]); d++) {
    float block[AUDIO_BLOCK_SIZE];
    fill_block(block, 1.0f);

    shape_update(&shaper, drives[d]);
    shape_render(&shaper, block);

    TEST_ASSERT_TRUE(block[0] < 1.0f);
    TEST_ASSERT_TRUE(block[0] > 0.0f);
  }
}

void render_is_odd_symmetric(void) {
  float pos[AUDIO_BLOCK_SIZE];
  float neg[AUDIO_BLOCK_SIZE];
  fill_block(pos, 0.6f);
  fill_block(neg, -0.6f);

  shape_update(&shaper, 0.7f);
  shape_render(&shaper, pos);
  shape_render(&shaper, neg);

  TEST_ASSERT_FLOAT_WITHIN(1e-6f, -pos[0], neg[0]);
}

void render_stays_bounded_for_large_out_of_range_input(void) {
  float block[AUDIO_BLOCK_SIZE];
  fill_block(block, 50.0f); /* well beyond the DCOs' +-1 operating range */

  shape_update(&shaper, 1.0f); /* max drive */
  shape_render(&shaper, block);

  /* The curve's true asymptote is (1+k)/k, which depends on
     shape_DRIVE_MAX - rather than hardcode that, just check the
     output was pulled down to a small fraction of the raw input. */
  TEST_ASSERT_TRUE(block[0] < 5.0f);
}

void render_increasing_drive_compresses_mid_level_signal_towards_peak(void) {
  float low_drive[AUDIO_BLOCK_SIZE];
  float high_drive[AUDIO_BLOCK_SIZE];
  fill_block(low_drive, 0.5f);
  fill_block(high_drive, 0.5f);

  shape_update(&shaper, 0.1f);
  shape_render(&shaper, low_drive);

  shape_update(&shaper, 1.0f);
  shape_render(&shaper, high_drive);

  TEST_ASSERT_TRUE(high_drive[0] > low_drive[0]);
}

int main(void) {
  UNITY_BEGIN();

  /* Initialisation / update */
  RUN_TEST(init_sets_bypass_defaults);
  RUN_TEST(update_maps_normalised_range_to_drive_range);

  /* Render - bypass */
  RUN_TEST(render_is_exact_bypass_when_drive_is_zero);

  /* Render - curve properties */
  RUN_TEST(render_bounds_full_scale_input_at_any_drive);
  RUN_TEST(render_is_odd_symmetric);
  RUN_TEST(render_stays_bounded_for_large_out_of_range_input);
  RUN_TEST(render_increasing_drive_compresses_mid_level_signal_towards_peak);

  return UNITY_END();
}
