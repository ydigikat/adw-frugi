/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   The DCA is a very simple implementation.  It exists mainly to set the output
   level and to apply any amplitude (volume) modulation.

   The AMP-EG is hard-wired to the amplitude to provide the sound's shape.

   LFO or MOD-EG can be applied for additional tremolo effects etc.

   Volume level parameter uses control smoothing to reduce stepping artefacts.

   The audio output is mono (the audio engine must replicate the signal to
   both left and right stereo channels).
   -----------------------------------------------------------------------------
*/
#include "dca.h"
#include "param.h"

/*
 * Time constant for control smoothing. This is used to smooth the
 * block-rate staircase caused by a volume control sweep.
 */
static const float PARAM_SMOOTH_TIME_MS = 10.0f;

void dca_init(struct dca *dca) {
  RTT_ASSERT(dca);

  dca->level = 0.0f;
  dca->target_level = 0.0f;
  dca->prev_scale = 0.0f;
  dca->has_scale = false;
  dca->smooth_coeff = param_smooth_coeff(PARAM_SMOOTH_TIME_MS);
}

void dca_note_on(struct dca *dca) {
  RTT_ASSERT(dca);

  dca->level = dca->target_level;

  /* New note has no previous value to smooth from, snap direct */
  dca->has_scale = false;
}

/*
 * Render the output applying amp envelope and other modulation.
 *
 * This employs a ramp to smooth out the stepping caused by the amp envs
 * block based approach.  The block approach is not musically offensive but
 * since the DCA has to iterate all samples anyway, it may as well apply
 * smoothing - a single multipler so not expensive.
 */
void dca_render(struct dca *dca, float sample_block[], float dca_env_level,
                float level_mod) {
  RTT_ASSERT(dca);
  RTT_ASSERT(sample_block);

  dca->level += (dca->target_level - dca->level) * dca->smooth_coeff;

  float *restrict ptr = sample_block;
  const float *restrict end = ptr + AUDIO_BLOCK_SIZE;

  const float mod = 1.0f + level_mod;
  const float scale = dca_env_level * dca->level * mod;

  if (dca->has_scale) {
    const float step = (scale - dca->prev_scale) / (float)AUDIO_BLOCK_SIZE;
    float cur = dca->prev_scale;

    #pragma clang loop unroll_count(4)
    while (ptr < end) {
      cur += step;
      *ptr++ *= cur;
    }
  } else {
    #pragma clang loop unroll_count(4)
    while (ptr < end) {
      *ptr++ *= scale;
    }
  }

  dca->prev_scale = scale;
  dca->has_scale = true;
}

void dca_update(struct dca *dca, float level) { dca->target_level = level; }
