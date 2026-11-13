/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
*/
#ifndef FRUGI_AMP_H
#define FRUGI_AMP_H

#include <stdbool.h>
#include <stddef.h>

struct dca {
  float level;
  float target_level;
  float smooth_coeff;
  float prev_scale;
  bool has_scale;
};

void dca_init(struct dca *dca);
void dca_note_on(struct dca *dca);
void dca_render(struct dca *dca, float sample_block[], float dca_env_level,
                float level_mod);
void dca_update(struct dca *dca, float level);

#endif /* FRUGI_AMP_H */
