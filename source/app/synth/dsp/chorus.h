/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
*/

#ifndef FRUGI_CHORUS_H
#define FRUGI_CHORUS_H

#include <stdint.h>

/*
 * Buffer sized power of two so the read/write indices can be wrapped with mask.  
 */
#define CHORUS_BUFFER_SIZE (2048u)
#define CHORUS_BUFFER_MASK (CHORUS_BUFFER_SIZE - 1u)

/*
 * Single delay line, dual tap stereo chorus.
 */
struct chorus {
  float buffer[CHORUS_BUFFER_SIZE];
  uint32_t write_pos;
  float lfo_phase;
  float lfo_inc;
};

/* API */
void chorus_init(struct chorus *chorus);
void chorus_reset(struct chorus *chorus);
void chorus_render(struct chorus *chorus, float *left, float *right);

#endif /* FRUGI_CHORUS_H */
