/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
*/
#ifndef FRUGI_MOD_MATRIX_H
#define FRUGI_MOD_MATRIX_H

#include <stddef.h>
#include "param.h"

/*
 * Modulation destinations (targets)
 */
enum mod_dest {
  MOD_DEST_DCO1_PITCH,
  MOD_DEST_DCO1_PWM,
  MOD_DEST_DCO2_PITCH,
  MOD_DEST_DCO2_PWM,
  MOD_DEST_DCF_CUTOFF,
  MOD_DEST_DCA_LEVEL,

  MOD_DEST_COUNT
};

/*
 * Modulation slot (a patch cable).
 */
struct mod_slot {
  enum mod_source source;
  enum mod_dest dest;
  float depth;
};

void mod_matrix_render(const struct mod_slot slots[], size_t count,
                       const float sources[MOD_SOURCE_COUNT],
                       float dest[MOD_DEST_COUNT]);

#endif /* FRUGI_MOD_MATRIX_H */
