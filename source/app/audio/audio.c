/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   The audio processing routine - the DAE.

   This is straightforward, driven by the superloop it monitors the
   audio interrupt line and executes a callback function into the synth to
   generate a block of audio samples when signalled.

   The same interrupt signal will cause any blocking routines to yield to the
   audio routine.

   The main job of the engine is to convert the floating point output from
   the synthesiser into the integer PCM format required by the I2S interface and
   refill the DMA audio buffer.

   There is a convenience function that will generate a 440Hz test tone during
   bring-up if no syntheiser code is yet available.  AUDIO_TEST_TONE should
   be defined to switch to using this.

   This is a lock-free implementation.
   -----------------------------------------------------------------------------
*/
#include <string.h>

#include "audio.h"
#include "board.h"
#include "trace.h"
#include "audio_defs.h"

#define AUDIO_BUFFER_SIZE (AUDIO_BLOCK_SIZE * 8)

/* Sample and audio buffers */
static float left_buffer[AUDIO_BLOCK_SIZE];
static float right_buffer[AUDIO_BLOCK_SIZE];

/*
 * PING/PONG buffer fed to I2S by DMA.
 */
static int16_t audio_buffer[AUDIO_BUFFER_SIZE];

/*
 * State shared with the DMA interrupt (audio_fill_buffer()), so volatile:
 * the ISR writes them and the superloop reads them.
 *
 * active_buffer : the half of audio_buffer the DMA has just finished
 *                 playing, and so the one to refill.
 * underruns     : requests that arrived while the previous one was still
 *                 being served - the block was late, so a stale half played.
 */
static volatile uint8_t active_buffer = PONG;
static volatile bool *audio_interrupt;
static volatile uint32_t underruns = 0;

static void audio_sanitise_buffer(float *buffer, int sampleCount);
#ifdef TRACE_AUDIO
static void audio_report_underruns(void);
#endif
static void audio_fill_buffer(uint8_t buffer_idx);
static void audio_test_tone(void *ctx, float *restrict left,
                            float *restrict right);

/*
 * Audio generator callback and its context.  The generator passes these in
 * the audio_init() call; this module never looks inside the context.
 */
static audio_block_fn process_block;
static void *process_block_ctx;

/*
 * When it gets an audio interrupt, the loop calls the audio generator to
 * process a new block and converts the float data into 16-bit words which
 * is stored in the audio buffer it shares with the I2S.
 */
void audio_routine() {
  if (*audio_interrupt) {
    
    /* Which half to fill */
    const uint8_t buffer = active_buffer;

    /* Generate sample block */
    process_block(process_block_ctx, left_buffer, right_buffer);

    /* Select the buffer to which audio is output */
    int16_t *restrict ptr = (buffer == PING)
                                ? audio_buffer
                                : audio_buffer + AUDIO_BUFFER_SIZE / 2;

#ifdef AUDIO_SANITISE_BUFFER
    /* Sanitises  buffers to protect speakers (ears) during development
     * (performance hit)*/
    audio_sanitise_buffer(left_buffer, AUDIO_BLOCK_SIZE);
    audio_sanitise_buffer(right_buffer, AUDIO_BLOCK_SIZE);
#endif

#pragma clang loop unroll_count(4)
    for (uint16_t i = 0; i < AUDIO_BLOCK_SIZE; i++) {

#ifdef TRACE_LEVELS
      if (fabsf(left_buffer[i]) > 1.0f || fabsf(right_buffer[i]) > 1.0f) {
        RTT_RED();
        RTT_LOG_FLOAT("Dac overshoot. Level:", left_buffer[i]);
        RTT_WHITE();
      }
#endif
      int32_t l_sample = left_buffer[i] * (float)INT32_MAX;
      int32_t r_sample = right_buffer[i] * (float)INT32_MAX;

      *ptr++ = (int16_t)(r_sample >> 16);
      *ptr++ = (int16_t)(r_sample);
      *ptr++ = (int16_t)(l_sample >> 16);
      *ptr++ = (int16_t)(l_sample);
    }

    /*
     * Done filling.  
     *
     * If a further request arrived while we were filling this buffer, we're
     * running late and will move straight onto filling the next request. This
     * will cause an audible glitch.
     */
          
    *audio_interrupt = false;

    if (active_buffer != buffer) {
      *audio_interrupt = true;
    }

#ifdef TRACE_AUDIO
    audio_report_underruns();
#endif
  }
}

/*
 * Initialises the audio routine, it just initialises the audio sub-system.
 */
bool audio_init(audio_block_fn process_block_function, void *ctx,
                volatile bool *audio_interrupt_flag) {
  RTT_ASSERT(process_block_function);

#ifdef AUDIO_TEST_TONE
  process_block = audio_test_tone;
#else
  process_block = process_block_function;
#endif
  process_block_ctx = ctx;

  /*
   * Must be set before audio_start().
   */
  audio_interrupt = audio_interrupt_flag;

  if (!audio_start(audio_buffer, AUDIO_BUFFER_SIZE, audio_fill_buffer)) {
    return false;
  }

  return true;
}

/*
 * Silences the output by zeroing the whole PING/PONG buffer.
 *
 * The I2S DMA is circular, so if the superloop stops servicing the audio
 * routine it keeps replaying whatever was in the last two half-buffers.
 */
void audio_silence(void) {
  memset(audio_buffer, 0, sizeof(audio_buffer));
}

/*
 * Callback from BSP to tell us to refill the specified buffer while
 * it sends the other.  The audio_interrupt flag signals the audio engine
 * to refill the buffer.
 */
static void audio_fill_buffer(uint8_t buffer_idx) {
  /*
   * Underrun detection.  If the previous request is still pending, its block
   * wasn't ready in time and the DMA has looped round. We just count and log
   * as any attempt to fix (eg mute) will still produce an audible glitch.
   */
  if (*audio_interrupt) {
    underruns++;
  }

  active_buffer = buffer_idx;
  *audio_interrupt = true;
}

/*
 * Total underruns since start-up - for diagnostics and tests.
 */
uint32_t audio_underrun_count(void) { return underruns; }

#ifdef TRACE_AUDIO
/*
 * Reports new underruns at most once a second, so that logging can't add
 * to an overload that is already causing them.
 */
static void audio_report_underruns(void) {
  static uint32_t reported = 0;
  static uint32_t blocks = 0;

  if (++blocks < AUDIO_SAMPLE_RATE / AUDIO_BLOCK_SIZE) {
    return;
  }
  blocks = 0;

  uint32_t total = underruns;
  if (total != reported) {
    RTT_LOG("%sAUDIO UNDERRUN: %u in the last second (%u total)\n",
            RTT_CTRL_TEXT_BRIGHT_RED, (unsigned)(total - reported),
            (unsigned)total);
    reported = total;
  }
}
#endif

#ifdef AUDIO_TEST_TONE

#include <math.h>

/* Coefficients for test tone generator */
static const float test_tone_b_coeff = 1.27323954474f;
static const float test_tone_c_coeff = -0.40528473456f;
static const float test_tone_p_coeff = 0.225f;
static float test_tone_phase = 0;
static float test_tone_inc = 440.0f / AUDIO_SAMPLE_RATE;

/*
 * This generates a sine approximation at 440Hz for testing. It can be passed
 * in as the audio callback function to check board bringup.
 */
static void audio_test_tone(void *ctx, float *restrict left,
                            float *restrict right) {
  (void)ctx;
  RTT_ASSERT(left != NULL);
  RTT_ASSERT(right != NULL);

  for (size_t i = 0; i < AUDIO_BLOCK_SIZE; i++) {
    if (test_tone_phase > 1.0f) {
      test_tone_phase -= 1.0f;
    }

    float angle = -1.0f * (test_tone_phase * 2.0f * 3.14159265f - 3.14159265f);
    float y =
        test_tone_b_coeff * angle + test_tone_c_coeff * angle * fabsf(angle);

    left[i] = (test_tone_p_coeff * (y * fabsf(y) - y) + y);
    right[i] = left[i];
    test_tone_phase += test_tone_inc;
  }
}

#endif

#ifdef AUDIO_SANITISE_BUFFER

#include <math.h>
#include <string.h>

/*
 * Sanitises the buffer to clear NaN, infinity and clipping, this
 * is unoptimised so only really useful during development to protect
 * speakers/ears.
 */
static void audio_sanitise_buffer(float *buffer, int sampleCount) {
  if (!buffer)
    return;

  for (int i = 0; i < sampleCount; ++i) {
    const float x = buffer[i];

    if (isnan(x) || isinf(x) || fabsf(x) > 2.0f) {
      memset(buffer, 0, sampleCount * sizeof(float));
      return;
    }

    if (fabsf(x) > 1.0f) {
      buffer[i] = x > 0 ? 1.0f : -1.0f;
    }
  }
}

#endif