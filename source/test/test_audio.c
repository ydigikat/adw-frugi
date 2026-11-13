/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   A.I. Disclosure:  These unit tests written by Anthropic Claude.
   -----------------------------------------------------------------------------
   Audio engine (DAE) tests.

   audio.c reaches the hardware only through audio_start(), so this file
   provides a stub that records the DMA buffer and the interrupt callback.
   Tests then play the part of the DMA interrupt by calling that callback -
   including from inside a render, to simulate a block that runs late.
   -----------------------------------------------------------------------------
*/

#include <string.h>

#include "audio.h"
#include "audio_defs.h"
#include "board.h"
#include "unity.h"

#define HALF_WORDS (AUDIO_BLOCK_SIZE * 4) /* int16 words in one half */

static int16_t *dma_buffer;
static void (*dma_isr)(uint8_t buffer_idx);
static bool fire_isr_during_start;

static volatile bool audio_flag;

/* Behaviour of the fake synth render */
static float render_level;
static bool fire_isr_during_render;
static uint8_t isr_half_during_render;
static int renders;
static int render_ctx; /* stands in for the synth - only its address matters */

/* --- stub of the I2S driver's audio_start() (normally i2s.c) --- */
bool audio_start(int16_t audio_buffer[], size_t len,
                 void (*render_audio_callback)(uint8_t buffer_idx)) {
  (void)len;
  dma_buffer = audio_buffer;
  dma_isr = render_audio_callback;

  if (fire_isr_during_start) {
    dma_isr(PING); /* the DMA's first interrupt, before audio_init returns */
  }

  return true;
}

static void fake_render(void *ctx, float *left, float *right) {
  TEST_ASSERT_EQUAL_PTR(&render_ctx, ctx); /* passed back unchanged */
  renders++;

  for (uint32_t i = 0; i < AUDIO_BLOCK_SIZE; i++) {
    left[i] = render_level;
    right[i] = render_level;
  }

  if (fire_isr_during_render) {
    fire_isr_during_render = false;
    dma_isr(isr_half_during_render); /* the block is running late */
  }
}

static bool half_is_silent(uint8_t half) {
  const int16_t *p = dma_buffer + (half == PING ? 0 : HALF_WORDS);
  for (uint32_t i = 0; i < HALF_WORDS; i++) {
    if (p[i] != 0) {
      return false;
    }
  }
  return true;
}

void setUp(void) {
  fire_isr_during_start = false;
  fire_isr_during_render = false;
  render_level = 0.5f;
  renders = 0;
  audio_flag = false;

  TEST_ASSERT_TRUE(audio_init(fake_render, &render_ctx, &audio_flag));
  audio_silence();
  audio_flag = false;
}

void tearDown(void) {}

/* C13: the DMA interrupt can fire before audio_init() returns */
void interrupt_during_start_is_safe(void) {
  /*
   * A different flag from the one setUp() registered: the early interrupt
   * must signal the flag passed to *this* call.  Previously the pointer was
   * set after audio_start(), so the interrupt wrote through whatever pointer
   * was there before - NULL at power-up, the old flag here.
   */
  static volatile bool start_flag = false;

  fire_isr_during_start = true;
  TEST_ASSERT_TRUE(audio_init(fake_render, &render_ctx, &start_flag));
  TEST_ASSERT_TRUE(start_flag);

  fire_isr_during_start = false;
  TEST_ASSERT_TRUE(audio_init(fake_render, &render_ctx, &audio_flag)); /* restore */
}

void normal_request_fills_the_requested_half(void) {
  dma_isr(PONG);
  audio_routine();

  TEST_ASSERT_FALSE(half_is_silent(PONG));
  TEST_ASSERT_TRUE(half_is_silent(PING));
  TEST_ASSERT_FALSE(audio_flag);
}

/*
 * C13: a late block is written to the half that requested it, even if the
 * next interrupt arrives mid-render.  Reading active_buffer after rendering
 * wrote into the other half - the one the DMA was playing.
 */
void late_block_is_written_to_the_half_that_requested_it(void) {
  fire_isr_during_render = true;
  isr_half_during_render = PONG;

  dma_isr(PING);
  audio_routine();

  TEST_ASSERT_FALSE(half_is_silent(PING));
  TEST_ASSERT_TRUE(half_is_silent(PONG));
}

/* C13: the request that arrived mid-render is served next, not lost */
void request_arriving_mid_render_is_served_next(void) {
  fire_isr_during_render = true;
  isr_half_during_render = PONG;

  dma_isr(PING);
  audio_routine();
  TEST_ASSERT_TRUE(audio_flag); /* still pending */

  audio_routine();
  TEST_ASSERT_EQUAL_INT(2, renders);
  TEST_ASSERT_FALSE(half_is_silent(PONG));
  TEST_ASSERT_FALSE(audio_flag);
}

/* C13: underrun detection */
void no_underruns_when_every_block_is_on_time(void) {
  uint32_t before = audio_underrun_count();

  for (int i = 0; i < 8; i++) {
    dma_isr(i & 1 ? PONG : PING);
    audio_routine();
  }

  TEST_ASSERT_EQUAL_UINT32(before, audio_underrun_count());
}

void request_while_previous_is_pending_counts_an_underrun(void) {
  uint32_t before = audio_underrun_count();

  dma_isr(PING);
  dma_isr(PONG); /* the PING block was never rendered */

  TEST_ASSERT_EQUAL_UINT32(before + 1, audio_underrun_count());
  audio_routine();
}

void late_render_counts_an_underrun(void) {
  uint32_t before = audio_underrun_count();

  fire_isr_during_render = true;
  isr_half_during_render = PONG;
  dma_isr(PING);
  audio_routine();

  TEST_ASSERT_EQUAL_UINT32(before + 1, audio_underrun_count());
  audio_routine();
}

/* audio_silence() (used around patch saves) zeroes both halves */
void silence_clears_both_halves(void) {
  dma_isr(PING);
  audio_routine();
  dma_isr(PONG);
  audio_routine();

  audio_silence();

  TEST_ASSERT_TRUE(half_is_silent(PING));
  TEST_ASSERT_TRUE(half_is_silent(PONG));
}

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(interrupt_during_start_is_safe);
  RUN_TEST(normal_request_fills_the_requested_half);
  RUN_TEST(late_block_is_written_to_the_half_that_requested_it);
  RUN_TEST(request_arriving_mid_render_is_served_next);
  RUN_TEST(no_underruns_when_every_block_is_on_time);
  RUN_TEST(request_while_previous_is_pending_counts_an_underrun);
  RUN_TEST(late_render_counts_an_underrun);
  RUN_TEST(silence_clears_both_halves);

  return UNITY_END();
}
