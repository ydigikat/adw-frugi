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

#include "display.h"
#include "unity.h"

static void clear_all_dirty(void) {
  for (uint8_t row = 0; row < DISPLAY_ROWS; row++) {
    display_render_row(row);
  }
}

void setUp(void) {  
  clear_all_dirty();
}

void tearDown(void) {}

/*
 * Independent re-implementation of display_render_row()'s glyph blit,
 * reading the font_ui[] table directly rather than going through
 * display.c's render_cell() - the two should always agree. Lets tests
 * assert exact pixel correctness for any real character without
 * hand-transcribing its bitmap.
 */
static uint16_t expected_cell_pixel(unsigned char ch, uint8_t cx, uint8_t cy,
                                    uint16_t fg, uint16_t bg) {
  if (ch < FONT_FIRST || ch > FONT_LAST) {
    return bg;
  }

  uint16_t bits = font_ui[ch - FONT_FIRST][cy];
  bool set = (bits & 1u << (FONT_CELL_W - 1u - cx)) != 0u;

  return set ? fg : bg;
}

static void assert_cell_matches(const uint16_t *px, uint8_t col,
                                unsigned char ch, uint16_t fg, uint16_t bg) {
  for (uint8_t y = 0; y < DISPLAY_CELL_H; y++) {
    for (uint8_t x = 0; x < DISPLAY_CELL_W; x++) {
      uint16_t expected = expected_cell_pixel(ch, x, y, fg, bg);
      uint16_t actual = px[(uint32_t)y * DISPLAY_ROW_PX_WIDTH +
                           (uint32_t)col * DISPLAY_CELL_W + x];
      TEST_ASSERT_EQUAL_UINT16(expected, actual);
    }
  }
}

/* -------------------------------------------------------------------------
 * clear / dirty tracking
 * ---------------------------------------------------------------------- */


void mark_all_dirty_sets_every_row(void) {
  /* setUp already flushed every row clean */
  for (uint8_t row = 0; row < DISPLAY_ROWS; row++) {
    TEST_ASSERT_FALSE(display_row_dirty(row));
  }

  display_mark_all_dirty();

  for (uint8_t row = 0; row < DISPLAY_ROWS; row++) {
    TEST_ASSERT_TRUE(display_row_dirty(row));
  }
}

void row_dirty_is_false_for_out_of_range_row(void) {
  TEST_ASSERT_FALSE(display_row_dirty(DISPLAY_ROWS));
  TEST_ASSERT_FALSE(display_row_dirty(255));
}

void render_row_returns_null_for_out_of_range_row(void) {
  TEST_ASSERT_NULL(display_render_row(DISPLAY_ROWS));
}

void render_row_clears_the_dirty_flag(void) {
  display_put_char(0, 3, 'A', COLOUR_FG, COLOUR_BG);
  TEST_ASSERT_TRUE(display_row_dirty(3));

  TEST_ASSERT_NOT_NULL(display_render_row(3));
  TEST_ASSERT_FALSE(display_row_dirty(3));
}

/* -------------------------------------------------------------------------
 * put_char
 * ---------------------------------------------------------------------- */

void put_char_marks_only_its_own_row_dirty(void) {
  display_put_char(5, 2, 'A', COLOUR_FG, COLOUR_BG);

  TEST_ASSERT_TRUE(display_row_dirty(2));
  for (uint8_t row = 0; row < DISPLAY_ROWS; row++) {
    if (row != 2) {
      TEST_ASSERT_FALSE(display_row_dirty(row));
    }
  }
}

void put_char_out_of_bounds_row_is_a_noop(void) {
  display_put_char(0, DISPLAY_ROWS, 'A', COLOUR_FG, COLOUR_BG);
  for (uint8_t row = 0; row < DISPLAY_ROWS; row++) {
    TEST_ASSERT_FALSE(display_row_dirty(row));
  }
}

void put_char_out_of_bounds_col_is_a_noop(void) {
  display_put_char(DISPLAY_COLS, 0, 'A', COLOUR_FG, COLOUR_BG);
  TEST_ASSERT_FALSE(display_row_dirty(0));
}

/* -------------------------------------------------------------------------
 * write_string - positioning and truncation
 * ---------------------------------------------------------------------- */

void write_string_out_of_bounds_row_is_a_noop(void) {
  display_write_string(0, DISPLAY_ROWS, "hello", COLOUR_FG, COLOUR_BG);
  for (uint8_t row = 0; row < DISPLAY_ROWS; row++) {
    TEST_ASSERT_FALSE(display_row_dirty(row));
  }
}

void write_string_out_of_bounds_col_is_a_noop(void) {
  display_write_string(DISPLAY_COLS, 0, "hello", COLOUR_FG, COLOUR_BG);
  TEST_ASSERT_FALSE(display_row_dirty(0));
}

void write_string_null_is_a_noop(void) {
  display_write_string(0, 0, NULL, COLOUR_FG, COLOUR_BG);
  TEST_ASSERT_FALSE(display_row_dirty(0));
}

void write_string_that_fits_reaches_its_last_character(void) {
  display_write_string(0, 1, "hi", COLOUR_FG, COLOUR_BG);

  const uint16_t *px = display_render_row(1);
  TEST_ASSERT_NOT_NULL(px);

  /* col 2 ('h','i' occupy col 0 and 1) must still be a blank space */
  assert_cell_matches(px, 2, ' ', COLOUR_FG, COLOUR_BG);
}

void write_string_truncates_at_right_edge_rather_than_wrapping(void) {
  char buf[DISPLAY_COLS + 6];
  for (int i = 0; i < DISPLAY_COLS + 5; i++) {
    buf[i] = 'M';
  }
  buf[DISPLAY_COLS + 5] = '\0';

  display_write_string(0, 4, buf, COLOUR_FG, COLOUR_BG);

  /* last column on the row was reached and written... */
  TEST_ASSERT_TRUE(display_row_dirty(4));
  const uint16_t *px = display_render_row(4);
  assert_cell_matches(px, (uint8_t)(DISPLAY_COLS - 1), 'M', COLOUR_FG,
                      COLOUR_BG);

  /* ...but the overflow was truncated, not wrapped onto the next row */
  TEST_ASSERT_FALSE(display_row_dirty(5));
}

void write_string_starting_at_the_last_column_writes_one_char_only(void) {
  display_write_string((uint8_t)(DISPLAY_COLS - 1), 4, "XY", COLOUR_FG,
                       COLOUR_BG);

  const uint16_t *px = display_render_row(4);
  assert_cell_matches(px, (uint8_t)(DISPLAY_COLS - 1), 'X', COLOUR_FG,
                      COLOUR_BG);
  /* 'Y' was truncated, not wrapped onto the next row */
  TEST_ASSERT_FALSE(display_row_dirty(5));
}

/* -------------------------------------------------------------------------
 * render_row - pixel content
 * ---------------------------------------------------------------------- */

void render_row_of_blank_cells_is_all_background(void) {
  const uint16_t *px = display_render_row(0);
  TEST_ASSERT_NOT_NULL(px);

  for (uint32_t i = 0; i < (uint32_t)DISPLAY_ROW_PX_WIDTH * DISPLAY_CELL_H;
       i++) {
    TEST_ASSERT_EQUAL_UINT16(COLOUR_BG, px[i]);
  }
}

void render_row_treats_out_of_font_range_char_as_blank(void) {
  /* 0x01 is below FONT_FIRST (0x20) - render_cell() must bounds-check
     rather than indexing the font table out of range. */
  display_put_char(0, 0, (unsigned char)0x01, COLOUR_FG, COLOUR_BG);
  const uint16_t *px = display_render_row(0);

  assert_cell_matches(px, 0, (unsigned char)0x01, COLOUR_FG, COLOUR_BG);
}

void render_row_matches_independently_computed_pixels_for_real_characters(
    void) {
  /* A mix of shapes (tall ascender, descender, digit, punctuation) and
     colour pairs across one row - exercises glyph bit placement and
     per-cell colour at once, each checked against expected_cell_pixel()'s
     independent re-derivation rather than a hand-picked pixel. */
  display_put_char(0, 5, '(', COLOUR_RED, COLOUR_BLACK);
  display_put_char(1, 5, 'g', COLOUR_GREEN, COLOUR_WHITE);
  display_put_char(2, 5, '0', COLOUR_BLUE, COLOUR_YELLOW);
  display_put_char(3, 5, ' ', COLOUR_FG, COLOUR_BG);

  const uint16_t *px = display_render_row(5);
  TEST_ASSERT_NOT_NULL(px);

  assert_cell_matches(px, 0, '(', COLOUR_RED, COLOUR_BLACK);
  assert_cell_matches(px, 1, 'g', COLOUR_GREEN, COLOUR_WHITE);
  assert_cell_matches(px, 2, '0', 0x001Fu, 0xFFE0u);
  assert_cell_matches(px, 3, ' ', COLOUR_FG, COLOUR_BG);
}

void get_char_reads_back_what_was_written(void) {
  display_put_char(3, 2, 'Q', COLOUR_FG, COLOUR_BG);
  TEST_ASSERT_EQUAL_UINT8('Q', display_get_char(3, 2));
}

void get_char_out_of_bounds_reads_a_space(void) {
  TEST_ASSERT_EQUAL_UINT8(' ', display_get_char(DISPLAY_COLS, 0));
  TEST_ASSERT_EQUAL_UINT8(' ', display_get_char(0, DISPLAY_ROWS));
}

int main(void) {
  UNITY_BEGIN();
  
  RUN_TEST(mark_all_dirty_sets_every_row);
  RUN_TEST(row_dirty_is_false_for_out_of_range_row);
  RUN_TEST(render_row_returns_null_for_out_of_range_row);
  RUN_TEST(render_row_clears_the_dirty_flag);

  RUN_TEST(put_char_marks_only_its_own_row_dirty);
  RUN_TEST(put_char_out_of_bounds_row_is_a_noop);
  RUN_TEST(put_char_out_of_bounds_col_is_a_noop);

  RUN_TEST(write_string_out_of_bounds_row_is_a_noop);
  RUN_TEST(write_string_out_of_bounds_col_is_a_noop);
  RUN_TEST(write_string_null_is_a_noop);
  RUN_TEST(write_string_that_fits_reaches_its_last_character);
  RUN_TEST(write_string_truncates_at_right_edge_rather_than_wrapping);
  RUN_TEST(write_string_starting_at_the_last_column_writes_one_char_only);

  RUN_TEST(render_row_of_blank_cells_is_all_background);
  RUN_TEST(render_row_treats_out_of_font_range_char_as_blank);
  RUN_TEST(
      render_row_matches_independently_computed_pixels_for_real_characters);

  RUN_TEST(get_char_reads_back_what_was_written);
  RUN_TEST(get_char_out_of_bounds_reads_a_space);

  return UNITY_END();
}
