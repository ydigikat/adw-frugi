/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
*/
#ifndef FRUGI_RING_MENU_H
#define FRUGI_RING_MENU_H

#include "param.h"
#include <stdint.h>
/*
 * Defines a page in the ring-menu.  The title shows in the ring-menu
 * bar at the top of the screen.
 */

/*
 * Page enums
 */
enum ring_page_id {
  RING_PAGE_DCO1,
  RING_PAGE_MOD1,
  RING_PAGE_DCO2,
  RING_PAGE_MOD2,
  RING_PAGE_NOISE,
  RING_PAGE_DCF,
  RING_PAGE_MOD_EG,
  RING_PAGE_LFO,
  RING_PAGE_DCA,
  RING_PAGE_PERF,
  RING_PAGE_COUNT
};

/*
 * State - which page and field we're on.
 */
struct ring_state {
  enum ring_page_id page;
  uint8_t sel_field;
};

/*
 * Row used for the selected field's hint - the bottom row.  A page that
 * puts a field on this row can't show hints (enforced by test_ring_menu.c).
 */
#define RING_HINT_ROW (DISPLAY_ROWS - 1)

/*
 * Optional field hint.  Called with the current patch when the field is
 * selected; returns a one-line hint (at most DISPLAY_COLS characters) or
 * NULL when there's nothing worth saying.
 */
typedef const char *(*ring_hint_fn)(const param_value_t *values);

/*
 * Ring field
 */
struct ring_field {
  enum param_id id;
  const char *label;
  uint8_t row;
  const char *const *enum_names;
  ring_hint_fn hint; /* NULL = no hint */
};

/*
 * Ring page, a collection of ring fields
 */
struct ring_page {
  const char *title;
  const struct ring_field *fields;
  uint8_t field_count;
};

/*
 * The ring page array.
 */
extern const struct ring_page ring_pages[RING_PAGE_COUNT];
extern const char *const mod_source_names[MOD_SOURCE_COUNT];

void ring_init(struct ring_state *ring_state);

void ring_next_page(struct ring_state *ring_state);
void ring_prev_page(struct ring_state *ring_state);
void ring_next_field(struct ring_state *ring_state);
void ring_prev_field(struct ring_state *ring_state);

enum param_id ring_selected_param(const struct ring_state *ring_state);
enum ring_page_id ring_current_page(const struct ring_state *ring_state);

param_value_t ring_adjust_value(const struct ring_state *ring_state,
                                param_value_t current, int8_t direction);

void render_ring(struct ring_state *ring_state, const param_value_t *values);

#endif /* FRUGI_RING_MENU_H */