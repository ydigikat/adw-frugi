/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   White noise generator.

   The only parameter here is level.  A RNG should create an even spectrum 
   generating a white noise output.
   -----------------------------------------------------------------------------
*/
#include <stdlib.h>

#include "noise.h"
#include "trace.h"

void noise_init(struct noise *noise, float gain) {
  noise->gain = gain;

  noise_reset(noise);
}

void noise_reset(struct noise *noise) { noise->level = 0; }

void noise_render(struct noise *noise, float sample_block[]) {
  RTT_ASSERT(noise);
  RTT_ASSERT(sample_block);

  if (noise->level == 0 || !noise->note_on) {
    return;
  }

  float *restrict ptr = sample_block;
  const float *restrict end = ptr + AUDIO_BLOCK_SIZE;

  while (ptr < end) {
    *ptr++ += (2.0f * ((float)rand() / (float)RAND_MAX) - 1.0f) * noise->level *
              noise->gain;
  }
}

void noise_note_on(struct noise *noise) {
  RTT_ASSERT(noise);
  noise->note_on = true;
}

void noise_note_off(struct noise *noise) {
  RTT_ASSERT(noise);
  noise->note_on = false;
}

void noise_update(struct noise *noise, float level) {
  RTT_ASSERT(noise);

  noise->level = level;
}