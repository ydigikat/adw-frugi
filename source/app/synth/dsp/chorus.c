/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   Stereo chorus.  Comes after the synth's voice summing mixer, not a per-voice
   or modulation-target effect - it has its own free-running rate and fixed
   depth/mix.

   One mono delay line is read by two independently modulated taps rather than
   running two delay lines. The right tap uses the same slow triangle LFO as
   the left but phase-inverted, so the two delay times swing apart and back
   together - this single-LFO trick is what gives classic BBD ensemble/chorus
   units (Juno, Dimension-D) their stereo width without needing a second
   oscillator.
   -----------------------------------------------------------------------------
*/
#include <math.h>

#include "chorus.h"
#include "trace.h"

/* going for a slow, wide Juno-like 'wobble' */
static const float CHORUS_RATE_HZ = 0.4f;       
static const float CHORUS_BASE_DELAY_MS = 18.0f;
static const float CHORUS_DEPTH_MS = 5.0f;
static const float CHORUS_MIX = 0.5f;           

/*
 * Initialise the chorus.
 */
void chorus_init(struct chorus *chorus) {
  RTT_ASSERT(chorus);

  chorus->lfo_inc = CHORUS_RATE_HZ / (float)AUDIO_SAMPLE_RATE;
  chorus_reset(chorus);
}

/*
 * Clears the delay line.  The line is only written while the chorus is
 * running, so when it is switched back on it still holds whatever was
 * playing when it was last switched off - call this on the off->on change
 * or that old audio is heard as a short burst through the wet taps.
 */
void chorus_reset(struct chorus *chorus) {
  RTT_ASSERT(chorus);

  for (uint32_t i = 0; i < CHORUS_BUFFER_SIZE; i++) {
    chorus->buffer[i] = 0.0f;
  }

  chorus->write_pos = 0;
  chorus->lfo_phase = 0.0f;
}

/*
 * Renders the chorus on summed stereo block. Left and right are mono on entry. 
 */
void chorus_render(struct chorus *chorus, float *left, float *right) {
  RTT_ASSERT(chorus);
  RTT_ASSERT(left);
  RTT_ASSERT(right);

  const float base_delay = CHORUS_BASE_DELAY_MS * AUDIO_SAMPLE_RATE / 1000.0f;
  const float depth = CHORUS_DEPTH_MS * AUDIO_SAMPLE_RATE / 1000.0f;

  float *restrict lp = left;
  float *restrict rp = right;
  const float *restrict end = lp + AUDIO_BLOCK_SIZE;

  float phase = chorus->lfo_phase;
  uint32_t write_pos = chorus->write_pos;

  while (lp < end) {
    float dry = *lp;

    chorus->buffer[write_pos] = dry;

    /* Triangle wave */
    float tri = 4.0f * fabsf(phase - 0.5f) - 1.0f;

    float delay_l = base_delay + depth * tri;
    float delay_r = base_delay - depth * tri;

    int32_t int_l = (int32_t)delay_l;
    float frac_l = delay_l - (float)int_l;
    uint32_t idx_l0 = (write_pos - (uint32_t)int_l) & CHORUS_BUFFER_MASK;
    uint32_t idx_l1 = (idx_l0 - 1u) & CHORUS_BUFFER_MASK;
    float wet_l = chorus->buffer[idx_l0] +
                  frac_l * (chorus->buffer[idx_l1] - chorus->buffer[idx_l0]);

    int32_t int_r = (int32_t)delay_r;
    float frac_r = delay_r - (float)int_r;
    uint32_t idx_r0 = (write_pos - (uint32_t)int_r) & CHORUS_BUFFER_MASK;
    uint32_t idx_r1 = (idx_r0 - 1u) & CHORUS_BUFFER_MASK;
    float wet_r = chorus->buffer[idx_r0] +
                  frac_r * (chorus->buffer[idx_r1] - chorus->buffer[idx_r0]);

    *lp++ = dry + CHORUS_MIX * (wet_l - dry);
    *rp++ = dry + CHORUS_MIX * (wet_r - dry);

    phase += chorus->lfo_inc;
    if (phase >= 1.0f) {
      phase -= 1.0f;
    }

    write_pos = (write_pos + 1u) & CHORUS_BUFFER_MASK;
  }

  chorus->lfo_phase = phase;
  chorus->write_pos = write_pos;
}
