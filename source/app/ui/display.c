#include <stddef.h>

#include "display.h"

/*
 * Implements a simple DISPLAY_COLS x DISPLAY_ROWS character based display.
 * The screen is a virtual representation of the text content which is
 * rendered into a row buffer for display.
 *
 * Transfer of the bitmap data to the physical device is done by the hardware
 * UI layer.
 */

/*
 * Character cell
 */
struct display_cell {
  unsigned char ch; /* character code */
  uint16_t fg;
  uint16_t bg;
};

/*
 * The cell array is the 'virtual screen'.
 */
static struct display_cell grid[DISPLAY_ROWS][DISPLAY_COLS];

/*
 * Bitmap holding dirty row flags, 1 bit per row.
 */
static uint16_t dirty_rows_bitmap;

/*
 * Partial frame-buffer.
 */
static uint16_t row_buffer[DISPLAY_ROW_PX_WIDTH * DISPLAY_CELL_H];

/*
 * Writes spaces to the screen and marks every row as dirty so that they are
 * all redrawn at the next render call.
 */
void display_clear(void) {
  for (uint8_t row = 0; row < DISPLAY_ROWS; row++) {
    for (uint8_t col = 0; col < DISPLAY_COLS; col++) {
      grid[row][col].ch = ' ';
      grid[row][col].fg = COLOUR_FG;
      grid[row][col].bg = COLOUR_BG;
    }
  }

  display_mark_all_dirty();
}


void display_mark_all_dirty(void) {
  dirty_rows_bitmap = (uint16_t)((1u << DISPLAY_ROWS) - 1u);
}

/*
 * Places a character onto the screen at position col,row with the specified
 * colours.
 *
 * Out of bound writes are ignored (no attempt made to wrap them).
 */
void display_put_char(uint8_t col, uint8_t row, unsigned char ch, uint16_t fg,
                      uint16_t bg) {
  if (row >= DISPLAY_ROWS || col >= DISPLAY_COLS) {
    return;
  }

  grid[row][col].ch = ch;
  grid[row][col].fg = fg;
  grid[row][col].bg = bg;
  dirty_rows_bitmap |= (uint16_t)(1u << row);
}

/*
 * Writes a string starting at col, row with the specified colours.
 *
 * Characters written outside of the screen bounds will not be displayed, string
 * will be truncated in that case.
 */
void display_write_string(uint8_t col, uint8_t row, const char *str,
                          uint16_t fg, uint16_t bg) {
  if (row >= DISPLAY_ROWS || col >= DISPLAY_COLS || str == NULL) {
    return;
  }

  for (uint8_t c = col; *str != '\0' && c < DISPLAY_COLS; c++, str++) {
    display_put_char(c, row, (unsigned char)*str, fg, bg);
  }
}

/*
 * Reads back the character at col,row (a space if out of bounds).  Lets
 * tests check what a menu actually drew.
 */
unsigned char display_get_char(uint8_t col, uint8_t row) {
  if (row >= DISPLAY_ROWS || col >= DISPLAY_COLS) {
    return ' ';
  }

  return grid[row][col].ch;
}

/*
 * Returns true if the row needs to be redrawn
 */
bool display_row_dirty(uint8_t row) {
  if (row >= DISPLAY_ROWS) {
    return false;
  }

  return (dirty_rows_bitmap & (uint16_t)(1u << row)) != 0;
}

/*
 * Renders one character cell.
 * Glyph rows come from the font table (already sized for the display cell).
 */
static void render_cell(uint16_t *cell, unsigned char ch, uint16_t fg,
                        uint16_t bg) {
  const uint16_t *glyph = NULL;

  if (ch >= FONT_FIRST && ch <= FONT_LAST) {
    glyph = font_ui[ch - FONT_FIRST];
  }

  for (uint8_t y = 0; y < DISPLAY_CELL_H; y++) {
    uint16_t bits = (glyph != NULL) ? glyph[y] : 0;
    uint16_t *dst_row = cell + (uint32_t)y * DISPLAY_ROW_PX_WIDTH;

    for (uint8_t x = 0; x < DISPLAY_CELL_W; x++) {
      dst_row[x] =
          (bits & ((uint16_t)1u << (DISPLAY_CELL_W - 1u - x))) ? fg : bg;
    }
  }
}

/*
 * Renders a row into the row buffer, one cell at a time.
 *
 * The caller must ensure this row has been rendered before calling it
 * for another row.
 */
const uint16_t *display_render_row(uint8_t row) {
  if (row >= DISPLAY_ROWS) {
    return NULL;
  }

  for (uint8_t col = 0; col < DISPLAY_COLS; col++) {
    render_cell(&row_buffer[(uint32_t)col * DISPLAY_CELL_W], grid[row][col].ch,
                grid[row][col].fg, grid[row][col].bg);
  }

  dirty_rows_bitmap &= (uint16_t)~(1u << row);

  return row_buffer;
}