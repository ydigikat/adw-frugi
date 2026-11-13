/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
*/
#ifndef FRUGI_LFO_H
#define FRUGI_LFO_H

#include <stdbool.h>
#include <stddef.h>
#include "param.h"


struct lfo {
  /* Params */
  float rate;
  enum lfo_mode mode;

  /* State */
  float phase;
  float inc;
  float fsr;
  float hold_time;
  float sh_value;
  float prev_phase;
};

/* API */
void lfo_init(struct lfo *lfo);
void lfo_reset(struct lfo *lfo);
void lfo_render(struct lfo *lfo, float *tri, float *saw, float *ramp,
                float *square, float *sand);
void lfo_update(struct lfo *lfo, float rate, enum lfo_mode mode);
void lfo_note_on(struct lfo *lfo);

#endif /* FRUGI_LFO_H */
