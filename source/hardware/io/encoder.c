/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   Encoders are read using TIM3 in encoder mode, the resulting up/down events
   are enqueued in a FIFO for polling by the UI routine.  In the same was as
   for buttons, this is preferable to using an ISR.

   Responding to UI navigation is not a time-critical operation.
   -----------------------------------------------------------------------------
*/

#include <stdbool.h>
#include <stdint.h>

#include "board.h"
#include "encoder.h"
#include "trace.h"

#include "stm32f4xx_ll_tim.h"

/*
 * Peripheral init
 */
bool enc_init(void) {
  LL_APB1_GRP1_EnableClock(ENC_TIM_PERIPH_CLK);
  LL_AHB1_GRP1_EnableClock(ENC_TIM_IO_CLK);

  LL_GPIO_InitTypeDef gpio = {.Pin = ENC_TIM_CH1_PIN | ENC_TIM_CH2_PIN,
                              .Mode = LL_GPIO_MODE_ALTERNATE,
                              .Alternate = ENC_TIM_AF,
                              .Pull = LL_GPIO_PULL_UP,
                              .OutputType = LL_GPIO_OUTPUT_PUSHPULL,
                              .Speed = LL_GPIO_SPEED_FREQ_LOW};

  if (LL_GPIO_Init(ENC_TIM_PORT, &gpio) != SUCCESS) {
    ERROR_LOG("Unable to initialise ENC_TIM IO");
    return false;
  }

  LL_TIM_IC_SetActiveInput(ENC_TIM, LL_TIM_CHANNEL_CH1,
                           LL_TIM_ACTIVEINPUT_DIRECTTI);
  LL_TIM_IC_SetPrescaler(ENC_TIM, LL_TIM_CHANNEL_CH1, LL_TIM_ICPSC_DIV1);
  LL_TIM_IC_SetFilter(ENC_TIM, LL_TIM_CHANNEL_CH1, LL_TIM_IC_FILTER_FDIV32_N8);
  LL_TIM_IC_SetPolarity(ENC_TIM, LL_TIM_CHANNEL_CH1, LL_TIM_IC_POLARITY_RISING);

  LL_TIM_IC_SetActiveInput(ENC_TIM, LL_TIM_CHANNEL_CH2,
                           LL_TIM_ACTIVEINPUT_DIRECTTI);
  LL_TIM_IC_SetPrescaler(ENC_TIM, LL_TIM_CHANNEL_CH2, LL_TIM_ICPSC_DIV1);
  LL_TIM_IC_SetFilter(ENC_TIM, LL_TIM_CHANNEL_CH2, LL_TIM_IC_FILTER_FDIV32_N8);
  LL_TIM_IC_SetPolarity(ENC_TIM, LL_TIM_CHANNEL_CH2, LL_TIM_IC_POLARITY_RISING);

  LL_TIM_InitTypeDef timer = {.CounterMode = LL_TIM_COUNTERMODE_UP,
                              .Prescaler = 0,
                              .Autoreload = 65535,
                              .ClockDivision = LL_TIM_CLOCKDIVISION_DIV1};

  if (LL_TIM_Init(ENC_TIM, &timer) != SUCCESS) {
    ERROR_LOG("Unable to initalise ENC_TIM peripheral");
    return false;
  }

  LL_TIM_SetEncoderMode(ENC_TIM, LL_TIM_ENCODERMODE_X2_TI1);
  LL_TIM_DisableARRPreload(ENC_TIM);
  LL_TIM_SetTriggerOutput(ENC_TIM, LL_TIM_TRGO_RESET);
  LL_TIM_DisableMasterSlaveMode(ENC_TIM);

  LL_TIM_EnableCounter(ENC_TIM);

  return true;
}

/*
 * We use a polling approach rather than interrupt here, this is to
 * ensure that the audio interrupt remains the only time significant driver.
 */
#define ENC_FIFO_SIZE (16)

static enum enc_event enc_fifo[ENC_FIFO_SIZE];
static uint8_t enc_fifo_head = 0;
static uint8_t enc_fifo_tail = 0;

static uint16_t enc_last_count = 0;
static int32_t enc_click_remainder = 0;

/*
 * Push a new event onto the FIFO.
 */
static void enc_fifo_push(enum enc_event event) {
  uint8_t next = (uint8_t)((enc_fifo_head + 1) % ENC_FIFO_SIZE);

  /* If this fires, we're not draining FIFO quickly enough */
  if (next == enc_fifo_tail) {
    RTT_LOG("Encoder buffer overflow");
    return;
  }

  enc_fifo[enc_fifo_head] = event;
  enc_fifo_head = next;
}

/*
 * Reads the hardware counter, converts movement since the previous call into
 * clicks, and pushes same number of events into the FIFO.  Called once on
 * each UI routine cycle.
 */
void enc_service(void) {
  uint16_t count = (uint16_t)LL_TIM_GetCounter(ENC_TIM);
  int16_t delta = (int16_t)(count - enc_last_count);
  enc_last_count = count;

  enc_click_remainder += delta;

  while (enc_click_remainder >= ENC_TIM_COUNTS_PER_CLICK) {
    enc_fifo_push(ENC_EVENT_UP);
    enc_click_remainder -= ENC_TIM_COUNTS_PER_CLICK;
  }

  while (enc_click_remainder <= -ENC_TIM_COUNTS_PER_CLICK) {
    enc_fifo_push(ENC_EVENT_DOWN);
    enc_click_remainder += ENC_TIM_COUNTS_PER_CLICK;
  }
}

/*
 * Pops event from FIFO. Returns false (and leaves *event
 * untouched) if FIFO empty. Call from loop to drain events.
 */
bool enc_poll_event(enum enc_event *event) {
  if (enc_fifo_tail == enc_fifo_head) {
    return false; /* empty */
  }

  *event = enc_fifo[enc_fifo_tail];
  enc_fifo_tail = (uint8_t)((enc_fifo_tail + 1) % ENC_FIFO_SIZE);

  return true;
}
