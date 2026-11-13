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

#include "patch.h"
#include "patch_store.h"
#include "unity.h"
#include <string.h>

static param_value_t params[PARAM_COUNT];
static uint8_t record[PATCH_STORE_SLOT_SIZE];

/*
 * Fill every parameter with a valid but distinct value for its type, so a
 * round trip that swaps or drops a parameter is caught.  Unpack validates
 * each value against param_desc[], so the fixture must be in range.
 */
static void fill_params(void) {
  memset(params, 0, sizeof(params));

  for (uint32_t i = 0; i < (uint32_t)PARAM_COUNT; i++) {
    const struct param_desc *d = &param_desc[i];

    switch (d->type) {
    case PARAM_TYPE_FLOAT:
      params[i].f = (float)(i % 50u) / 50.0f;
      break;
    case PARAM_TYPE_ENUM:
      params[i].e = i % d->enum_count;
      break;
    case PARAM_TYPE_INT:
      params[i].i = d->min + (int32_t)(i % (uint32_t)(d->max - d->min + 1));
      break;
    case PARAM_TYPE_BOOL:
      params[i].b = (i & 1u) != 0;
      break;
    case PARAM_TYPE_UNDEFINED:
    default:
      break; /* the VOICE_PARAM_COUNT marker slot */
    }
  }
}

void setUp() {
  fill_params();
  memset(record, 0, sizeof(record));
}

void tearDown() {}

/* -------------------------------------------------------------------------
 * Round trip
 * ---------------------------------------------------------------------- */

void pack_then_unpack_round_trips_params_and_name(void) {
  TEST_ASSERT_TRUE(patch_store_pack(record, params, "Fat Saw"));

  param_value_t out_params[PARAM_COUNT];
  char name[PATCH_NAME_LEN];
  TEST_ASSERT_TRUE(patch_store_unpack(record, out_params, name));

  TEST_ASSERT_EQUAL_STRING("Fat Saw", name);
  TEST_ASSERT_EQUAL_MEMORY(params, out_params, sizeof(params));
}

void pack_with_null_name_round_trips_empty_string(void) {
  TEST_ASSERT_TRUE(patch_store_pack(record, params, NULL));

  char name[PATCH_NAME_LEN];
  TEST_ASSERT_TRUE(patch_store_unpack(record, NULL, name));
  TEST_ASSERT_EQUAL_STRING("", name);
}

void pack_rejects_name_that_does_not_fit(void) {
  /* PATCH_NAME_LEN includes the NUL, so a name of exactly that many
     characters (no room for the terminator) must be rejected. */
  char too_long[PATCH_NAME_LEN + 1];
  memset(too_long, 'A', sizeof(too_long) - 1);
  too_long[sizeof(too_long) - 1] = '\0';

  TEST_ASSERT_FALSE(patch_store_pack(record, params, too_long));
}

void unpack_can_fetch_only_params_or_only_name(void) {
  TEST_ASSERT_TRUE(patch_store_pack(record, params, "Pad"));

  param_value_t out_params[PARAM_COUNT];
  memset(out_params, 0, sizeof(out_params));
  TEST_ASSERT_TRUE(patch_store_unpack(record, out_params, NULL));
  TEST_ASSERT_EQUAL_MEMORY(params, out_params, sizeof(params));

  char name[PATCH_NAME_LEN];
  TEST_ASSERT_TRUE(patch_store_unpack(record, NULL, name));
  TEST_ASSERT_EQUAL_STRING("Pad", name);
}

void unpack_null_terminates_a_name_that_fills_the_whole_field(void) {
  char exact[PATCH_NAME_LEN];
  memset(exact, 'B', sizeof(exact));
  exact[PATCH_NAME_LEN - 1] = '\0'; /* longest name that still fits */

  TEST_ASSERT_TRUE(patch_store_pack(record, params, exact));

  char name[PATCH_NAME_LEN];
  TEST_ASSERT_TRUE(patch_store_unpack(record, NULL, name));
  TEST_ASSERT_EQUAL_STRING(exact, name);
}

/* -------------------------------------------------------------------------
 * Occupied / rejection cases
 * ---------------------------------------------------------------------- */

void erased_eeprom_pattern_is_not_occupied(void) {
  memset(record, 0xFF, sizeof(record));

  TEST_ASSERT_FALSE(patch_store_is_occupied(record));

  param_value_t out_params[PARAM_COUNT];
  char name[PATCH_NAME_LEN];
  TEST_ASSERT_FALSE(patch_store_unpack(record, out_params, name));
}

void zeroed_buffer_is_not_occupied(void) {
  TEST_ASSERT_FALSE(patch_store_is_occupied(record));
}

void packed_record_is_occupied(void) {
  TEST_ASSERT_TRUE(patch_store_pack(record, params, "X"));
  TEST_ASSERT_TRUE(patch_store_is_occupied(record));
}

void unpack_rejects_mismatched_format_version(void) {
  TEST_ASSERT_TRUE(patch_store_pack(record, params, "X"));
  record[1] += 1; /* version byte */

  param_value_t out_params[PARAM_COUNT];
  char name[PATCH_NAME_LEN];
  TEST_ASSERT_FALSE(patch_store_unpack(record, out_params, name));
}

void unpack_rejects_mismatched_param_count(void) {
  TEST_ASSERT_TRUE(patch_store_pack(record, params, "X"));
  record[2] += 1; /* low byte of the stored param_count */

  param_value_t out_params[PARAM_COUNT];
  char name[PATCH_NAME_LEN];
  TEST_ASSERT_FALSE(patch_store_unpack(record, out_params, name));
}

void unpack_rejects_corrupted_payload(void) {
  TEST_ASSERT_TRUE(patch_store_pack(record, params, "X"));
  record[PATCH_STORE_HEADER_SIZE] ^= 0xFFu; /* flip first payload byte */

  param_value_t out_params[PARAM_COUNT];
  char name[PATCH_NAME_LEN];
  TEST_ASSERT_FALSE(patch_store_unpack(record, out_params, name));
}

void unpack_rejects_corrupted_name(void) {
  TEST_ASSERT_TRUE(patch_store_pack(record, params, "X"));
  record[4] ^= 0xFFu; /* first byte of the name field */

  param_value_t out_params[PARAM_COUNT];
  char name[PATCH_NAME_LEN];
  TEST_ASSERT_FALSE(patch_store_unpack(record, out_params, name));
}

/* -------------------------------------------------------------------------
 * Value validation (C16)
 *
 * A record can pass its CRC and still hold values this firmware can't use -
 * e.g. saved after the C23 bug wrote float bits into an enum, or by a build
 * whose enums differed with the same PARAM_COUNT.  Enum values index jump
 * tables in the DSP code, so they are range-checked before loading.
 * ---------------------------------------------------------------------- */

static bool pack_and_unpack(param_value_t out[PARAM_COUNT]) {
  TEST_ASSERT_TRUE(patch_store_pack(record, params, "Bad"));
  return patch_store_unpack(record, out, NULL);
}

void unpack_accepts_the_default_patch(void) {
  memset(params, 0, sizeof(params));
  param_create_default_patch(params);

  param_value_t out[PARAM_COUNT];
  TEST_ASSERT_TRUE(pack_and_unpack(out));
}

void unpack_accepts_values_at_the_edges_of_their_ranges(void) {
  params[DCA_LEVEL].f = 0.0f;
  params[DCF_CUTOFF].f = 1.0f;
  params[DCO1_WAVE].e = DCO_WAVE_COUNT - 1;
  params[DCO1_OCTAVE].i = -OSC_OCTAVE_RANGE;
  params[DCO2_OCTAVE].i = OSC_OCTAVE_RANGE;

  param_value_t out[PARAM_COUNT];
  TEST_ASSERT_TRUE(pack_and_unpack(out));
}

void unpack_rejects_an_out_of_range_enum(void) {
  params[DCF_MODE].e = FILTER_COUNT; /* one past the last mode */

  param_value_t out[PARAM_COUNT];
  TEST_ASSERT_FALSE(pack_and_unpack(out));
}

/* The value a C23-era save holds: float 0.01 bits in an enum (~1e9) */
void unpack_rejects_float_bits_in_an_enum(void) {
  params[DCO1_PWM_SOURCE].f = 0.01f;

  param_value_t out[PARAM_COUNT];
  TEST_ASSERT_FALSE(pack_and_unpack(out));
}

void unpack_rejects_a_float_outside_zero_to_one(void) {
  param_value_t out[PARAM_COUNT];

  params[DCA_LEVEL].f = 1.5f;
  TEST_ASSERT_FALSE(pack_and_unpack(out));

  fill_params();
  params[DCA_LEVEL].f = -0.25f;
  TEST_ASSERT_FALSE(pack_and_unpack(out));
}

void unpack_rejects_a_nan_or_infinite_float(void) {
  param_value_t out[PARAM_COUNT];

  params[DCF_RES].f = NAN;
  TEST_ASSERT_FALSE(pack_and_unpack(out));

  fill_params();
  params[DCF_RES].f = INFINITY;
  TEST_ASSERT_FALSE(pack_and_unpack(out));
}

void unpack_rejects_an_out_of_range_int(void) {
  params[DCO1_SEMI].i = OSC_SEMI_RANGE + 1;

  param_value_t out[PARAM_COUNT];
  TEST_ASSERT_FALSE(pack_and_unpack(out));
}

void unpack_rejects_a_bool_that_is_not_zero_or_one(void) {
  params[DCF_TRACK].e = 2; /* raw byte 2 in the bool's storage */

  param_value_t out[PARAM_COUNT];
  TEST_ASSERT_FALSE(pack_and_unpack(out));
}

/* A rejected record leaves the caller's parameters untouched */
void rejected_record_does_not_modify_the_output(void) {
  params[DCF_MODE].e = FILTER_COUNT;
  TEST_ASSERT_TRUE(patch_store_pack(record, params, "Bad"));

  param_value_t out[PARAM_COUNT];
  memset(out, 0x5A, sizeof(out));
  param_value_t before[PARAM_COUNT];
  memcpy(before, out, sizeof(out));

  TEST_ASSERT_FALSE(patch_store_unpack(record, out, NULL));
  TEST_ASSERT_EQUAL_MEMORY(before, out, sizeof(out));
}

/*
 * The patch browser reads names only.  A record with a valid CRC but invalid
 * values still has a genuine name, so it is listed (and can be deleted) - it
 * just can't be loaded.  A record that fails its CRC is not listed at all
 * (unpack_rejects_corrupted_name).
 */
void name_only_unpack_still_lists_a_record_with_invalid_values(void) {
  params[DCF_MODE].e = FILTER_COUNT;
  TEST_ASSERT_TRUE(patch_store_pack(record, params, "Bad"));

  char name[PATCH_NAME_LEN];
  TEST_ASSERT_TRUE(patch_store_unpack(record, NULL, name));
  TEST_ASSERT_EQUAL_STRING("Bad", name);

  param_value_t out[PARAM_COUNT];
  TEST_ASSERT_FALSE(patch_store_unpack(record, out, NULL)); /* won't load */
}

/* -------------------------------------------------------------------------
 * Slot addressing
 * ---------------------------------------------------------------------- */

void slot_addr_is_slot_times_slot_size(void) {
  TEST_ASSERT_EQUAL_UINT16(0, patch_store_slot_addr(0));
  TEST_ASSERT_EQUAL_UINT16(PATCH_STORE_SLOT_SIZE, patch_store_slot_addr(1));
  TEST_ASSERT_EQUAL_UINT16(10 * PATCH_STORE_SLOT_SIZE,
                           patch_store_slot_addr(10));
}

void record_size_fits_within_slot_size(void) {
  TEST_ASSERT_TRUE(PATCH_STORE_RECORD_SIZE <= PATCH_STORE_SLOT_SIZE);
}

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(pack_then_unpack_round_trips_params_and_name);
  RUN_TEST(pack_with_null_name_round_trips_empty_string);
  RUN_TEST(pack_rejects_name_that_does_not_fit);
  RUN_TEST(unpack_can_fetch_only_params_or_only_name);
  RUN_TEST(unpack_null_terminates_a_name_that_fills_the_whole_field);

  RUN_TEST(erased_eeprom_pattern_is_not_occupied);
  RUN_TEST(zeroed_buffer_is_not_occupied);
  RUN_TEST(packed_record_is_occupied);
  RUN_TEST(unpack_rejects_mismatched_format_version);
  RUN_TEST(unpack_rejects_mismatched_param_count);
  RUN_TEST(unpack_rejects_corrupted_payload);
  RUN_TEST(unpack_rejects_corrupted_name);

  RUN_TEST(unpack_accepts_the_default_patch);
  RUN_TEST(unpack_accepts_values_at_the_edges_of_their_ranges);
  RUN_TEST(unpack_rejects_an_out_of_range_enum);
  RUN_TEST(unpack_rejects_float_bits_in_an_enum);
  RUN_TEST(unpack_rejects_a_float_outside_zero_to_one);
  RUN_TEST(unpack_rejects_a_nan_or_infinite_float);
  RUN_TEST(unpack_rejects_an_out_of_range_int);
  RUN_TEST(unpack_rejects_a_bool_that_is_not_zero_or_one);
  RUN_TEST(rejected_record_does_not_modify_the_output);
  RUN_TEST(name_only_unpack_still_lists_a_record_with_invalid_values);

  RUN_TEST(slot_addr_is_slot_times_slot_size);
  RUN_TEST(record_size_fits_within_slot_size);

  return UNITY_END();
}
