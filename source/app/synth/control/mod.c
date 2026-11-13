/* -----------------------------------------------------------------------------
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   The modulation matrix is a very simple implementation.  You can consider
   each slot a patch cord where each cord routes it source (scaled by depth)
   to a destination.  Multiple slots can apply to the same destination (values
   are summed).  The render 'runs' each slot to update the destination.
  -----------------------------------------------------------------------------
*/
#include "mod.h"

/*
 * Render recalculates the matrix for one block.  It zeroes the destination
 * value then accumulates (source_value * depth) into its destination.
 *
 * This implementation does not apply any transformations or range clamping
 * during the calculation.
 */
void mod_matrix_render(const struct mod_slot slots[], size_t count,
                       const float sources[MOD_SOURCE_COUNT],
                       float dest[MOD_DEST_COUNT]) {
  for (int i = 0; i < MOD_DEST_COUNT; i++) {
    dest[i] = 0.0f;
  }

  for (size_t i = 0; i < count; i++) {
    dest[slots[i].dest] += sources[slots[i].source] * slots[i].depth;
  }
}
