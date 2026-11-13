/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   A basic wave-shaper.  This comes after the DCO1+DCO2+NOISE are mixed and 
   shapes their summed signals. 
   
   The nature of this algorithm results in low-drive signals at unity level 
   being rounded off - there is effectively no curve which will be identity at 
   low-drive and provide a fixed ceiling for large peaks.
   -----------------------------------------------------------------------------
*/
#include <math.h>

#include "param.h"
#include "trace.h"
#include "shape.h"

#define WAVESHAPER_DRIVE_MIN (0.0f)
#define WAVESHAPER_DRIVE_MAX (5.0f)

/*
 * Initialise the waveshaper.
 */
void shape_init(struct shaper *shaper) {
  RTT_ASSERT(shaper);

  shaper->drive_param = 0.0f;
  shaper->drive = WAVESHAPER_DRIVE_MIN;
}

/*
 * Update. Quadratic taper.
 */
void shape_update(struct shaper *shaper, float drive) {
  RTT_ASSERT(shaper);

  shaper->drive_param = drive;
  shaper->drive =
      param_uni_to_quadratic(drive, WAVESHAPER_DRIVE_MIN, WAVESHAPER_DRIVE_MAX);
}

/*
 * Renders the wave shaper.
 */
void shape_render(struct shaper *shaper, float sample_block[]) {
  RTT_ASSERT(shaper);
  RTT_ASSERT(sample_block);

  /* Nothing to do when the drive is off - skip the block entirely. */
  if (shaper->drive <= 0.0f) {
    return;
  }

  float *restrict ptr = sample_block;
  const float *restrict end = ptr + AUDIO_BLOCK_SIZE;

  /* Stack caching of variables used in the sample generation loop */
  const float k = shaper->drive;
  const float gain = 1.0f + k;

  #pragma clang loop unroll_count(4)

  while (ptr < end) {    
    float gx = *ptr * gain;
    *ptr++ = gx / (1.0f + fabsf(gx));
  }
}
