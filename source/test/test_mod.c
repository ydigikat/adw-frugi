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

#include "mod.h"
#include "unity.h"

static float sources[MOD_SOURCE_COUNT];
static float dest[MOD_DEST_COUNT];

void setUp(void) {
  for (int i = 0; i < MOD_SOURCE_COUNT; i++)
    sources[i] = 0.0f;
  for (int i = 0; i < MOD_DEST_COUNT; i++)
    dest[i] = -1.0f; /* distinctive sentinel - a real render always overwrites
                        every dest */
}

void tearDown(void) {}

/* -------------------------------------------------------------------------
 * Basic routing
 * ---------------------------------------------------------------------- */

void no_slots_zeroes_every_destination(void) {
  mod_matrix_render(NULL, 0, sources, dest);

  for (int i = 0; i < MOD_DEST_COUNT; i++)
    TEST_ASSERT_EQUAL_FLOAT(0.0f, dest[i]);
}

void single_slot_routes_source_times_depth_to_its_dest(void) {
  struct mod_slot slots[] = {
      {LFO1_TRI, MOD_DEST_DCF_CUTOFF, 0.5f},
  };
  sources[LFO1_TRI] = 0.8f;

  mod_matrix_render(slots, 1, sources, dest);

  TEST_ASSERT_EQUAL_FLOAT(0.4f, dest[MOD_DEST_DCF_CUTOFF]);
}

void unrouted_destinations_stay_zero(void) {
  struct mod_slot slots[] = {
      {LFO1_TRI, MOD_DEST_DCF_CUTOFF, 0.5f},
  };
  sources[LFO1_TRI] = 0.8f;

  mod_matrix_render(slots, 1, sources, dest);

  TEST_ASSERT_EQUAL_FLOAT(0.0f, dest[MOD_DEST_DCO1_PITCH]);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, dest[MOD_DEST_DCO1_PWM]);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, dest[MOD_DEST_DCO2_PITCH]);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, dest[MOD_DEST_DCO2_PWM]);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, dest[MOD_DEST_DCA_LEVEL]);
}

void zero_depth_contributes_nothing_regardless_of_source(void) {
  struct mod_slot slots[] = {
      {LFO1_SQUARE, MOD_DEST_DCA_LEVEL, 0.0f},
  };
  sources[LFO1_SQUARE] = 1.0f;

  mod_matrix_render(slots, 1, sources, dest);

  TEST_ASSERT_EQUAL_FLOAT(0.0f, dest[MOD_DEST_DCA_LEVEL]);
}

/* -------------------------------------------------------------------------
 * One-to-many: a single source feeding several destinations
 * ---------------------------------------------------------------------- */

void one_source_can_feed_multiple_destinations_independently(void) {
  struct mod_slot slots[] = {
      {ENV1_LEVEL, MOD_DEST_DCO1_PITCH, 0.25f},
      {ENV1_LEVEL, MOD_DEST_DCF_CUTOFF, 1.0f},
  };
  sources[ENV1_LEVEL] = 0.6f;

  mod_matrix_render(slots, 2, sources, dest);

  TEST_ASSERT_EQUAL_FLOAT(0.15f, dest[MOD_DEST_DCO1_PITCH]);
  TEST_ASSERT_EQUAL_FLOAT(0.6f, dest[MOD_DEST_DCF_CUTOFF]);
}

/* -------------------------------------------------------------------------
 * Many-to-one: several slots summing into the same destination
 * ---------------------------------------------------------------------- */

void many_to_one_sums_contributions_at_the_shared_destination(void) {
  struct mod_slot slots[] = {
      {LFO1_TRI, MOD_DEST_DCA_LEVEL, 0.3f},
      {ENV1_LEVEL, MOD_DEST_DCA_LEVEL, 0.5f},
      {LFO1_SQUARE, MOD_DEST_DCA_LEVEL, 0.2f},
  };
  sources[LFO1_TRI] = 1.0f;
  sources[ENV1_LEVEL] = 1.0f;
  sources[LFO1_SQUARE] = 1.0f;

  mod_matrix_render(slots, 3, sources, dest);

  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f,
                           dest[MOD_DEST_DCA_LEVEL]); /* 0.3 + 0.5 + 0.2 */
}

void negative_depth_can_cancel_a_positive_contribution(void) {
  struct mod_slot slots[] = {
      {LFO1_TRI, MOD_DEST_DCF_CUTOFF, 1.0f},
      {ENV1_LEVEL, MOD_DEST_DCF_CUTOFF, -1.0f},
  };
  sources[LFO1_TRI] = 0.5f;
  sources[ENV1_LEVEL] = 0.5f;

  mod_matrix_render(slots, 2, sources, dest);

  TEST_ASSERT_EQUAL_FLOAT(0.0f, dest[MOD_DEST_DCF_CUTOFF]);
}

/* -------------------------------------------------------------------------
 * Re-render behaviour - dest is fully recomputed, not accumulated
 * across calls (a stale value from a prior block must not leak forward).
 * ---------------------------------------------------------------------- */

void dest_is_recomputed_not_accumulated_across_renders(void) {
  struct mod_slot slots[] = {
      {LFO1_TRI, MOD_DEST_DCO1_PITCH, 1.0f},
  };

  sources[LFO1_TRI] = 1.0f;
  mod_matrix_render(slots, 1, sources, dest);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, dest[MOD_DEST_DCO1_PITCH]);

  sources[LFO1_TRI] = 0.0f;
  mod_matrix_render(slots, 1, sources, dest);
  TEST_ASSERT_EQUAL_FLOAT(
      0.0f, dest[MOD_DEST_DCO1_PITCH]); /* not still 1.0 from the prior block */
}

int main(void) {
  UNITY_BEGIN();

  /* Basic routing */
  RUN_TEST(no_slots_zeroes_every_destination);
  RUN_TEST(single_slot_routes_source_times_depth_to_its_dest);
  RUN_TEST(unrouted_destinations_stay_zero);
  RUN_TEST(zero_depth_contributes_nothing_regardless_of_source);

  /* One-to-many */
  RUN_TEST(one_source_can_feed_multiple_destinations_independently);

  /* Many-to-one */
  RUN_TEST(many_to_one_sums_contributions_at_the_shared_destination);
  RUN_TEST(negative_depth_can_cancel_a_positive_contribution);

  /* Re-render behaviour */
  RUN_TEST(dest_is_recomputed_not_accumulated_across_renders);

  return UNITY_END();
}
