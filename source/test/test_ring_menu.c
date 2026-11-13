/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   A.I. Disclosure:  These unit tests written by Anthropic Claude.
   -----------------------------------------------------------------------------
   Ring menu table validation.

   The ring pages are hand-written tables of (param id, label, row, enum
   names).  An entry pointing at the wrong parameter compiles cleanly and
   only shows up on hardware - as a control that edits something else, or an
   enum rendered without its names table (a debug-build assert, garbage in
   release).  These tests check every table entry against the parameter
   descriptors so that class of mistake fails here instead.
   -----------------------------------------------------------------------------
*/

#include <string.h>

#include "display.h"
#include "param.h"
#include "patch.h"
#include "ring_menu.h"
#include "unity.h"

static param_value_t params[PARAM_COUNT];

void setUp(void) {
  memset(params, 0, sizeof(params));
  param_create_default_patch(params);
}

void tearDown(void) {}

/* Every field refers to a real parameter, not the VOICE_PARAM_COUNT marker */
void test_every_field_id_is_a_real_parameter(void) {
  for (int p = 0; p < RING_PAGE_COUNT; p++) {
    const struct ring_page *page = &ring_pages[p];

    for (int f = 0; f < page->field_count; f++) {
      enum param_id id = page->fields[f].id;
      TEST_ASSERT_TRUE_MESSAGE(id < PARAM_COUNT, page->fields[f].label);
      TEST_ASSERT_NOT_EQUAL_MESSAGE(VOICE_PARAM_COUNT, id,
                                    page->fields[f].label);
    }
  }
}

/*
 * Enum parameters need a names table to render, and every name must exist.
 * Non-enum parameters must not have one - a names table on a float field
 * means the id is wrong (this is how the PWM Depth rows were mis-wired).
 */
void test_enum_fields_have_complete_names_and_others_have_none(void) {
  for (int p = 0; p < RING_PAGE_COUNT; p++) {
    const struct ring_page *page = &ring_pages[p];

    for (int f = 0; f < page->field_count; f++) {
      const struct ring_field *field = &page->fields[f];
      const struct param_desc *desc = &param_desc[field->id];

      if (desc->type == PARAM_TYPE_ENUM) {
        TEST_ASSERT_NOT_NULL_MESSAGE(field->enum_names, field->label);

        for (uint32_t e = 0; e < desc->enum_count; e++) {
          TEST_ASSERT_NOT_NULL_MESSAGE(field->enum_names[e], field->label);
        }
      } else {
        TEST_ASSERT_NULL_MESSAGE(field->enum_names, field->label);
      }
    }
  }
}

/*
 * A parameter appears on at most one row of the whole ring.  A duplicate
 * almost always means a copy-pasted entry kept the wrong id (as DCO2's
 * PWM Depth did), leaving the intended parameter with no control at all.
 */
void test_no_parameter_appears_on_two_rows(void) {
  const char *seen[PARAM_COUNT] = {0};

  for (int p = 0; p < RING_PAGE_COUNT; p++) {
    const struct ring_page *page = &ring_pages[p];

    for (int f = 0; f < page->field_count; f++) {
      const struct ring_field *field = &page->fields[f];

      TEST_ASSERT_NULL_MESSAGE(seen[field->id], field->label);
      seen[field->id] = field->label;
    }
  }
}

/* Row 0 is the status bar; fields sit on distinct rows below it */
void test_rows_are_on_screen_and_unique_within_a_page(void) {
  for (int p = 0; p < RING_PAGE_COUNT; p++) {
    const struct ring_page *page = &ring_pages[p];
    bool used[DISPLAY_ROWS] = {false};

    for (int f = 0; f < page->field_count; f++) {
      uint8_t row = page->fields[f].row;

      TEST_ASSERT_TRUE_MESSAGE(row >= 1 && row < DISPLAY_ROWS,
                               page->fields[f].label);
      TEST_ASSERT_FALSE_MESSAGE(used[row], page->fields[f].label);
      used[row] = true;
    }
  }
}

/* Labels leave room on the row for at least a short value */
void test_labels_fit_the_row(void) {
  for (int p = 0; p < RING_PAGE_COUNT; p++) {
    const struct ring_page *page = &ring_pages[p];

    for (int f = 0; f < page->field_count; f++) {
      TEST_ASSERT_TRUE_MESSAGE(strlen(page->fields[f].label) <=
                                   DISPLAY_COLS - 5,
                               page->fields[f].label);
    }
  }
}

/* Every page renders with every field selected in turn */
void test_every_page_and_field_renders(void) {
  struct ring_state state;
  ring_init(&state);

  for (int p = 0; p < RING_PAGE_COUNT; p++) {
    state.page = (enum ring_page_id)p;

    for (uint8_t f = 0; f < ring_pages[p].field_count; f++) {
      state.sel_field = f;
      set_param_changed(true);
      render_ring(&state, params);
      TEST_ASSERT_FALSE(get_param_changed());
    }
  }
}

/* The encoder edits the parameter the selected row is labelled with */
void test_selected_param_matches_the_table(void) {
  struct ring_state state;
  ring_init(&state);

  for (int p = 0; p < RING_PAGE_COUNT; p++) {
    state.page = (enum ring_page_id)p;

    for (uint8_t f = 0; f < ring_pages[p].field_count; f++) {
      state.sel_field = f;
      TEST_ASSERT_EQUAL(ring_pages[p].fields[f].id,
                        ring_selected_param(&state));
    }
  }
}

/* -------------------------------------------------------------------------
 * Rendered output (C20)
 * ---------------------------------------------------------------------- */

/* Read one display row back as a string */
static void read_row(uint8_t row, char out[DISPLAY_COLS + 1]) {
  for (uint8_t col = 0; col < DISPLAY_COLS; col++) {
    out[col] = (char)display_get_char(col, row);
  }
  out[DISPLAY_COLS] = '\0';
}

static bool ends_with(const char *s, const char *suffix) {
  size_t n = strlen(s), m = strlen(suffix);
  return m <= n && strcmp(s + n - m, suffix) == 0;
}

/*
 * Every value of every enum field is drawn in full, right-aligned, with the
 * label intact on the left.  The value buffer used to be 8 bytes, which cut
 * "INV+BIAS" to "INV+BIA".
 */
void every_enum_value_is_drawn_in_full_beside_its_label(void) {
  struct ring_state state;
  ring_init(&state);

  for (int p = 0; p < RING_PAGE_COUNT; p++) {
    state.page = (enum ring_page_id)p;
    state.sel_field = 0;

    for (uint8_t f = 0; f < ring_pages[p].field_count; f++) {
      const struct ring_field *field = &ring_pages[p].fields[f];
      const struct param_desc *desc = &param_desc[field->id];

      if (desc->type != PARAM_TYPE_ENUM) {
        continue;
      }

      for (uint32_t e = 0; e < desc->enum_count; e++) {
        char row[DISPLAY_COLS + 1];

        params[field->id].e = e;
        set_param_changed(true);
        render_ring(&state, params);
        read_row(field->row, row);

        TEST_ASSERT_TRUE_MESSAGE(ends_with(row, field->enum_names[e]),
                                 field->enum_names[e]);
        TEST_ASSERT_EQUAL_INT_MESSAGE(
            0, strncmp(row, field->label, strlen(field->label)), field->label);
      }
    }
  }
}

/* Titles are all 5 characters so the status bar stays aligned */
void page_titles_are_all_the_same_width(void) {
  for (int p = 0; p < RING_PAGE_COUNT; p++) {
    TEST_ASSERT_EQUAL_size_t_MESSAGE(5, strlen(ring_pages[p].title),
                                     ring_pages[p].title);
  }
}

/* -------------------------------------------------------------------------
 * Field hints
 * ---------------------------------------------------------------------- */

/* A page with a hinted field keeps the hint row free of fields */
void pages_with_hints_leave_the_hint_row_free(void) {
  for (int p = 0; p < RING_PAGE_COUNT; p++) {
    const struct ring_page *page = &ring_pages[p];
    bool has_hint = false, uses_hint_row = false;

    for (int f = 0; f < page->field_count; f++) {
      has_hint |= page->fields[f].hint != NULL;
      uses_hint_row |= page->fields[f].row == RING_HINT_ROW;
    }

    TEST_ASSERT_FALSE_MESSAGE(has_hint && uses_hint_row, page->title);
  }
}

/* Index of `id` on the NOISE page */
static uint8_t noise_field(enum param_id id) {
  const struct ring_page *page = &ring_pages[RING_PAGE_NOISE];
  for (uint8_t f = 0; f < page->field_count; f++) {
    if (page->fields[f].id == id) {
      return f;
    }
  }
  TEST_FAIL_MESSAGE("field not on NOISE page");
  return 0;
}

/* Render the NOISE page with `id` selected; return the hint row, trimmed */
static const char *hint_row_for(enum param_id id) {
  static char row[DISPLAY_COLS + 1];
  struct ring_state state;
  ring_init(&state);
  state.page = RING_PAGE_NOISE;
  state.sel_field = noise_field(id);

  set_param_changed(true);
  render_ring(&state, params);
  read_row(RING_HINT_ROW, row);

  for (int i = DISPLAY_COLS - 1; i >= 0 && row[i] == ' '; i--) {
    row[i] = '\0';
  }
  return row;
}

#define DETUNE_HINT "Detune DCO2 to ring"
#define PURE_HINT "DCO1 Lvl 0: pure RM"

void ringmod_hint_is_blank_when_ring_mod_is_off(void) {
  params[DCO_RINGMOD_LEVEL].f = 0.0f;
  TEST_ASSERT_EQUAL_STRING("", hint_row_for(DCO_RINGMOD_LEVEL));
}

void ringmod_hint_asks_to_detune_at_unison(void) {
  params[DCO_RINGMOD_LEVEL].f = 0.5f; /* default patch: DCO2 at unison */
  TEST_ASSERT_EQUAL_STRING(DETUNE_HINT, hint_row_for(DCO_RINGMOD_LEVEL));
}

void ringmod_hint_asks_to_detune_at_octave_and_fifth(void) {
  params[DCO_RINGMOD_LEVEL].f = 1.0f;

  params[DCO2_OCTAVE].i = 1;
  TEST_ASSERT_EQUAL_STRING(DETUNE_HINT, hint_row_for(DCO_RINGMOD_LEVEL));

  params[DCO2_OCTAVE].i = 0;
  params[DCO2_SEMI].i = 7;
  params[DCO2_CENTS].f = 0.52f; /* +2 cents is still a fifth */
  TEST_ASSERT_EQUAL_STRING(DETUNE_HINT, hint_row_for(DCO_RINGMOD_LEVEL));
}

void ringmod_hint_measures_the_interval_from_dco1(void) {
  params[DCO_RINGMOD_LEVEL].f = 1.0f;

  /* Both up a fifth: still unison with each other */
  params[DCO1_SEMI].i = 7;
  params[DCO2_SEMI].i = 7;
  TEST_ASSERT_EQUAL_STRING(DETUNE_HINT, hint_row_for(DCO_RINGMOD_LEVEL));

  /* DCO2 unchanged at 0, DCO1 a tritone up: inharmonic */
  params[DCO1_SEMI].i = 6;
  params[DCO2_SEMI].i = 0;
  TEST_ASSERT_EQUAL_STRING(PURE_HINT, hint_row_for(DCO_RINGMOD_LEVEL));
}

void ringmod_hint_suggests_muting_dco1_once_detuned(void) {
  params[DCO_RINGMOD_LEVEL].f = 1.0f;
  params[DCO2_SEMI].i = 7;
  params[DCO2_CENTS].f = 0.63f; /* +13 cents */
  TEST_ASSERT_EQUAL_STRING(PURE_HINT, hint_row_for(DCO_RINGMOD_LEVEL));

  /* Already pure ring: nothing to say */
  params[DCO1_LEVEL].f = 0.0f;
  TEST_ASSERT_EQUAL_STRING("", hint_row_for(DCO_RINGMOD_LEVEL));
}

void hint_clears_when_another_field_is_selected(void) {
  params[DCO_RINGMOD_LEVEL].f = 1.0f;
  TEST_ASSERT_EQUAL_STRING(DETUNE_HINT, hint_row_for(DCO_RINGMOD_LEVEL));
  TEST_ASSERT_EQUAL_STRING("", hint_row_for(NOISE_LEVEL));
}

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(test_every_field_id_is_a_real_parameter);
  RUN_TEST(test_enum_fields_have_complete_names_and_others_have_none);
  RUN_TEST(test_no_parameter_appears_on_two_rows);
  RUN_TEST(test_rows_are_on_screen_and_unique_within_a_page);
  RUN_TEST(test_labels_fit_the_row);
  RUN_TEST(test_every_page_and_field_renders);
  RUN_TEST(test_selected_param_matches_the_table);
  RUN_TEST(every_enum_value_is_drawn_in_full_beside_its_label);
  RUN_TEST(page_titles_are_all_the_same_width);
  RUN_TEST(pages_with_hints_leave_the_hint_row_free);
  RUN_TEST(ringmod_hint_is_blank_when_ring_mod_is_off);
  RUN_TEST(ringmod_hint_asks_to_detune_at_unison);
  RUN_TEST(ringmod_hint_asks_to_detune_at_octave_and_fifth);
  RUN_TEST(ringmod_hint_measures_the_interval_from_dco1);
  RUN_TEST(ringmod_hint_suggests_muting_dco1_once_detuned);
  RUN_TEST(hint_clears_when_another_field_is_selected);

  return UNITY_END();
}
