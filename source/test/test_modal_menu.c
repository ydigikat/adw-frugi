/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   A.I. Disclosure:  These unit tests written by Anthropic Claude.
   -----------------------------------------------------------------------------
   Modal (patch load/save/delete) menu tests.

   The EEPROM-backed patch store and the audio engine are replaced by stubs
   in this file, so the tests can see exactly what the menu asked them to do
   and read back what it drew (display_get_char()).
   -----------------------------------------------------------------------------
*/

#include <string.h>

#include "audio.h"
#include "display.h"
#include "modal_menu.h"
#include "patch_persist.h"
#include "unity.h"

/* --- stub patch store: a name per slot, plus call records --- */
static char slot_names[PATCH_PERSIST_SLOT_COUNT][PATCH_NAME_LEN];
static bool load_ok;
static int saves, deletes, silences;
static uint16_t last_slot;
static char last_saved_name[PATCH_NAME_LEN];

bool patch_persist_slot_info(uint16_t slot, bool *occupied,
                             char name_out[PATCH_NAME_LEN]) {
  *occupied = slot_names[slot][0] != '\0';
  memcpy(name_out, slot_names[slot], PATCH_NAME_LEN);
  return true;
}

bool patch_persist_load(uint16_t slot, param_value_t params[PARAM_COUNT],
                        char name_out[PATCH_NAME_LEN]) {
  last_slot = slot;
  if (!load_ok) {
    return false;
  }
  params[DCA_LEVEL].f = 0.75f; /* marker the test can see */
  memcpy(name_out, slot_names[slot], PATCH_NAME_LEN);
  return true;
}

bool patch_persist_save(uint16_t slot, const param_value_t params[PARAM_COUNT],
                        const char *name) {
  (void)params;
  saves++;
  last_slot = slot;
  strncpy(last_saved_name, name, PATCH_NAME_LEN - 1);
  strncpy(slot_names[slot], name, PATCH_NAME_LEN - 1);
  return true;
}

bool patch_persist_delete(uint16_t slot) {
  deletes++;
  last_slot = slot;
  slot_names[slot][0] = '\0';
  return true;
}

void audio_silence(void) { silences++; }

/* --- helpers --- */
static param_value_t current[PARAM_COUNT];
static param_value_t loaded[PARAM_COUNT];

static void read_row(uint8_t row, char out[DISPLAY_COLS + 1]) {
  for (uint8_t col = 0; col < DISPLAY_COLS; col++) {
    out[col] = (char)display_get_char(col, row);
  }
  out[DISPLAY_COLS] = '\0';
}

static bool row_contains(uint8_t row, const char *text) {
  char buf[DISPLAY_COLS + 1];
  render_modal();
  read_row(row, buf);
  return strstr(buf, text) != NULL;
}

static void select_action(int steps) { /* from LOAD */
  modal_next_field(); /* SLOT -> ACTION */
  for (int i = 0; i < steps; i++) {
    modal_encoder(1);
  }
}

void setUp(void) {
  memset(slot_names, 0, sizeof(slot_names));
  strcpy(slot_names[0], "INIT PAD");
  load_ok = true;
  saves = deletes = silences = 0;
  memset(last_saved_name, 0, sizeof(last_saved_name));
  memset(current, 0, sizeof(current));
  memset(loaded, 0, sizeof(loaded));

  display_clear();
  modal_init();
}

void tearDown(void) {}

/* ------------------------------------------------------------------------- */

void opens_on_slot_0_showing_its_name_and_load(void) {
  TEST_ASSERT_TRUE(row_contains(2, "000"));
  TEST_ASSERT_TRUE(row_contains(3, "INIT PAD"));
  TEST_ASSERT_TRUE(row_contains(4, "LOAD"));
}

void empty_slot_is_shown_as_empty(void) {
  modal_encoder(1);
  TEST_ASSERT_TRUE(row_contains(2, "001"));
  TEST_ASSERT_TRUE(row_contains(3, "-- empty --"));
}

void encoder_steps_the_slot_and_wraps(void) {
  modal_encoder(-1);
  TEST_ASSERT_TRUE(row_contains(2, "127"));
  modal_encoder(1);
  TEST_ASSERT_TRUE(row_contains(2, "000"));
}

void left_and_right_jump_ten_slots_and_wrap(void) {
  modal_right();
  TEST_ASSERT_TRUE(row_contains(2, "010"));
  modal_left();
  modal_left();
  TEST_ASSERT_TRUE(row_contains(2, "118"));
}

void action_field_cycles_load_save_delete(void) {
  select_action(1);
  TEST_ASSERT_TRUE(row_contains(4, "SAVE"));
  modal_encoder(1);
  TEST_ASSERT_TRUE(row_contains(4, "DELETE"));
  modal_encoder(1);
  TEST_ASSERT_TRUE(row_contains(4, "LOAD"));
}

void load_returns_the_patch_and_reports_success(void) {
  TEST_ASSERT_TRUE(modal_confirm(current, loaded));
  TEST_ASSERT_EQUAL_FLOAT(0.75f, loaded[DCA_LEVEL].f);
  TEST_ASSERT_TRUE(row_contains(8, "Loaded."));
}

/* A record that fails validation (C16) is listed but won't load */
void load_failure_is_reported_and_installs_nothing(void) {
  load_ok = false;
  TEST_ASSERT_FALSE(modal_confirm(current, loaded));
  TEST_ASSERT_TRUE(row_contains(8, "Load failed."));
}

void save_edits_the_name_then_saves_with_silenced_audio(void) {
  select_action(1);                          /* SAVE */
  TEST_ASSERT_FALSE(modal_confirm(current, loaded)); /* -> name entry */
  TEST_ASSERT_TRUE(row_contains(6, "INIT PAD"));     /* existing name */

  modal_encoder(1); /* 'I' -> 'J' */
  TEST_ASSERT_FALSE(modal_confirm(current, loaded)); /* save */

  TEST_ASSERT_EQUAL_INT(1, saves);
  TEST_ASSERT_EQUAL_STRING("JNIT PAD", last_saved_name); /* trailing spaces trimmed */
  TEST_ASSERT_TRUE(silences >= 1); /* O1: output silenced for the stall */
  TEST_ASSERT_TRUE(row_contains(8, "Saved."));
  TEST_ASSERT_TRUE(row_contains(3, "JNIT PAD"));
}

void name_cursor_stays_inside_the_name_field(void) {
  select_action(1);
  modal_confirm(current, loaded);

  for (int i = 0; i < 40; i++) {
    modal_right(); /* past the end */
  }
  modal_encoder(1); /* last position: ' ' -> 'A' */
  modal_confirm(current, loaded);

  TEST_ASSERT_EQUAL_size_t(PATCH_NAME_LEN - 1, strlen(last_saved_name));
}

void delete_needs_confirming_and_any_other_key_cancels(void) {
  select_action(2); /* DELETE */
  modal_confirm(current, loaded);
  TEST_ASSERT_TRUE(row_contains(6, "Delete slot 000?"));

  modal_encoder(1); /* any other control cancels */
  TEST_ASSERT_EQUAL_INT(0, deletes);
  TEST_ASSERT_TRUE(row_contains(3, "INIT PAD"));

  modal_confirm(current, loaded); /* DELETE again */
  modal_confirm(current, loaded); /* confirm */
  TEST_ASSERT_EQUAL_INT(1, deletes);
  TEST_ASSERT_TRUE(silences >= 1);
  TEST_ASSERT_TRUE(row_contains(8, "Deleted."));
  TEST_ASSERT_TRUE(row_contains(3, "-- empty --"));
}

/* C20: the delete hint fits the 20-column row in full */
void delete_hint_fits_on_one_row(void) {
  select_action(2);
  modal_confirm(current, loaded);
  TEST_ASSERT_TRUE(row_contains(7, "Hold C=yes, else no"));
}

void leaving_the_page_cancels_name_entry(void) {
  select_action(1);
  modal_confirm(current, loaded); /* name entry */
  modal_on_hidden();
  modal_on_shown();

  TEST_ASSERT_TRUE(row_contains(7, "Hold C SAVE")); /* back to browsing */
  TEST_ASSERT_EQUAL_INT(0, saves);
}

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(opens_on_slot_0_showing_its_name_and_load);
  RUN_TEST(empty_slot_is_shown_as_empty);
  RUN_TEST(encoder_steps_the_slot_and_wraps);
  RUN_TEST(left_and_right_jump_ten_slots_and_wrap);
  RUN_TEST(action_field_cycles_load_save_delete);
  RUN_TEST(load_returns_the_patch_and_reports_success);
  RUN_TEST(load_failure_is_reported_and_installs_nothing);
  RUN_TEST(save_edits_the_name_then_saves_with_silenced_audio);
  RUN_TEST(name_cursor_stays_inside_the_name_field);
  RUN_TEST(delete_needs_confirming_and_any_other_key_cancels);
  RUN_TEST(delete_hint_fits_on_one_row);
  RUN_TEST(leaving_the_page_cancels_name_entry);

  return UNITY_END();
}
