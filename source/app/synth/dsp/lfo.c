/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   These are naive oscillators for control-rate signals (LFOs)

   This LFO is architected based on early subtractives, the CS20M I once owned
   using this technique.  All LFO waveforms are generated concurrently, with a 
   single shared rate.  Each target can select which wave and depth to modulate
   by, but not an independent rate. This is an efficient way of getting a 
   useful number of modulator waveforms at a relatively low cost.

   The classic LFO waves are included. TRI,SAW, RAMP, SQUARE and SAMPLE & HOLD.

   The S&H uses a deterministic pseudo RNG (xorshift) rather than the math.h
   rand() function.  Why? Just because.
   -----------------------------------------------------------------------------
*/
#include "lfo.h"

static inline float lfo_triangle(struct lfo *lfo, float phase);
static inline float lfo_saw(struct lfo *lfo, float phase);
static inline float lfo_ramp(struct lfo *lfo, float phase);
static inline float lfo_square(struct lfo *lfo, float phase);
static inline float lfo_sample_and_hold(struct lfo *lfo);

/*
 * Initialise the LFO.
 */
void lfo_init(struct lfo *lfo) {
  RTT_ASSERT(lfo);

  lfo->hold_time = -1;
  lfo->sh_value = 0;
  lfo_reset(lfo);
}

/*
 * Resets the phase
 *
 * All our waves are generated concurrently so triangle wave is going
 * to be out of phase, since waves are not mixed this is OK.
 */
void lfo_reset(struct lfo *lfo) {
  RTT_ASSERT(lfo);

  lfo->phase = 0.0f;
}

/*
 * This generates naive waves unsuitable for use as audio but fine for LFO.
 *
 * The LFO is inspired by early LFOs such as that on the Yamaha CS20M. All
 * waves are generated concurrently at the same rate.  A target can choose
 * which wave to apply and how much, but the rate is global.
 */
void lfo_render(struct lfo *lfo, float *tri, float *saw, float *ramp,
                float *square, float *sandh) {
  RTT_ASSERT(lfo);
  RTT_ASSERT(tri);
  RTT_ASSERT(saw);
  RTT_ASSERT(ramp);
  RTT_ASSERT(square);
  RTT_ASSERT(sandh);

  const float next_phase =
      fmodf(lfo->phase + (lfo->inc * (float)AUDIO_BLOCK_SIZE), 1.0f);

  *tri = lfo_triangle(lfo, lfo->phase);
  *saw = lfo_saw(lfo, lfo->phase);
  *ramp = lfo_ramp(lfo, lfo->phase);
  *square = lfo_square(lfo, lfo->phase);
  *sandh = lfo_sample_and_hold(lfo);

  lfo->phase = next_phase;
}

/*
 * Refresh the LFO parameters.
 */
void lfo_update(struct lfo *lfo, float rate, enum lfo_mode mode) {
  RTT_ASSERT(lfo);

  lfo->rate = param_uni_to_linear(rate, 0, 20);
  lfo->mode = mode;

  lfo->inc = lfo->rate / AUDIO_SAMPLE_RATE;
}

/*
 * Retrigger the LFO on a note on event if the mode is
 * set to NOTE (not free-running)
 */
void lfo_note_on(struct lfo *lfo) {
  RTT_ASSERT(lfo);

  if (lfo->mode == LFO_MODE_NOTE) {
    lfo_reset(lfo);
  }
}

/*
 * Generate a simple triangle wave.
 */
static inline float lfo_triangle(struct lfo *lfo, float phase) {
  return 4.0f * (float)fabsf(phase - 0.5f) - 1.0f;
}

/*
 * Generate a simple sawtooth wave
 */
static inline float lfo_saw(struct lfo *lfo, float phase) {
  return (2.0f * lfo->phase - 1.0f);
}

/*
 * Inverted sawtooth wave.
 */
static inline float lfo_ramp(struct lfo *lfo, float phase) {
  return 1.0f - (2.0f * lfo->phase);
}

/*
 * Generate a square wave.
 */
static inline float lfo_square(struct lfo *lfo, float phase) {
  return lfo->phase > 0.5f ? -1.0f : 1.0f;
}

/*
 * RNG state.
 */
static uint32_t xorshift32(uint32_t *state) {
  uint32_t x = *state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  *state = x;
  return x;
}

/*
 * Sample and hold.  This uses a simple pseudo random
 * number generator (RNG).
 */
static inline float lfo_sample_and_hold(struct lfo *lfo) {
  static uint32_t rand_state = 2463534242; /* Seed for XORShift RNG */

  if (lfo->phase < lfo->prev_phase) {
    uint32_t random_int = xorshift32(&rand_state);
    lfo->sh_value = (float)random_int / (float)UINT32_MAX * 2.0f - 1.0f;
  }

  lfo->prev_phase = lfo->phase;

  return lfo->sh_value;
}
