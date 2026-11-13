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

#include "dca.h"
#include "unity.h"

static struct dca dca;
static float block[AUDIO_BLOCK_SIZE];

static void fill_block(float value) {
  for (uint16_t i = 0; i < AUDIO_BLOCK_SIZE; i++)
    block[i] = value;
}

void setUp(void) {
  dca_init(&dca);
  fill_block(1.0f);
}

void tearDown(void) {}

/* -------------------------------------------------------------------------
 * Initialisation
 * ---------------------------------------------------------------------- */

void init_sets_zero_level(void) {
  TEST_ASSERT_EQUAL_FLOAT(0.0f, dca.level);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, dca.target_level);
}

void init_computes_smoothing_coefficient_in_valid_range(void) {
  /* A coefficient outside (0,1) would mean either no smoothing at all
     (0) or an instant snap every block (>=1) - see Part 2, "Parameter
     smoothing" for what this coefficient is doing. */
  TEST_ASSERT_TRUE(dca.smooth_coeff > 0.0f);
  TEST_ASSERT_TRUE(dca.smooth_coeff < 1.0f);
}

/* -------------------------------------------------------------------------
 * Note-on snap
 *
 * A fresh note must sound at the current patch level immediately - only a
 * change arriving mid-note is smoothed. See dcf_note_on()'s equivalent
 * snap in test_dcf.c.
 * ---------------------------------------------------------------------- */

void note_on_snaps_level_to_target(void) {
  dca_update(&dca, 0.8f);
  dca_note_on(&dca);

  TEST_ASSERT_EQUAL_FLOAT(0.8f, dca.level);
}

void render_scales_block_by_current_level(void) {
  dca_update(&dca, 0.5f);
  dca_note_on(&dca);

  dca_render(&dca, block, 1.0f, 0.0f);

  TEST_ASSERT_EQUAL_FLOAT(0.5f, block[0]);
}

/* -------------------------------------------------------------------------
 * Mid-note smoothing
 *
 * A parameter change with no accompanying note-on (a CC/knob sweep while
 * a note is already sounding) must approach the new target gradually,
 * not jump straight to it - that jump is the zipper-noise hazard Part 2
 * describes.
 * ---------------------------------------------------------------------- */

void update_without_note_on_moves_level_gradually(void) {
  dca_update(&dca, 1.0f);
  dca_note_on(&dca); /* start fully open */

  dca_update(&dca, 0.0f); /* sweep towards silence mid-note */
  dca_render(&dca, block, 1.0f, 0.0f);

  TEST_ASSERT_TRUE(dca.level > 0.0f);
  TEST_ASSERT_TRUE(dca.level < 1.0f);
}

void repeated_renders_converge_on_new_target(void) {
  dca_update(&dca, 1.0f);
  dca_note_on(&dca);

  dca_update(&dca, 0.25f);
  for (int i = 0; i < 200; i++)
    dca_render(&dca, block, 1.0f, 0.0f);

  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.25f, dca.level);
}

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(init_sets_zero_level);
  RUN_TEST(init_computes_smoothing_coefficient_in_valid_range);

  RUN_TEST(note_on_snaps_level_to_target);
  RUN_TEST(render_scales_block_by_current_level);

  RUN_TEST(update_without_note_on_moves_level_gradually);
  RUN_TEST(repeated_renders_converge_on_new_target);

  return UNITY_END();
}
