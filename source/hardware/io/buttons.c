/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   Buttons are read, debounced and enqueued in a FIFO which is periodically
   polled (drained) by the ui routine in the superloop.  This is preferable
   to using an ISR as I want the audio interrupt to be the only timing driver
   in the application.

   Responding to UI navigation is not a time-critical operation.
   -----------------------------------------------------------------------------
*/
#include "buttons.h"
#include "board.h"
#include "trace.h"

/*
 * These buttons align with the ANO Navigator control I typically use. Of course
 * they are usable with a separate encoder/button and set of 4 buttons.
 */
static const uint32_t btn_pin[BTN_COUNT] = {[BTN_N] = NAV_BTN_N_PIN,
                                            [BTN_E] = NAV_BTN_E_PIN,
                                            [BTN_S] = NAV_BTN_S_PIN,
                                            [BTN_W] = NAV_BTN_W_PIN,
                                            [BTN_C] = NAV_BTN_C_PIN};

/*
 * Per-button debounce state.
 */
struct btn_state {
  bool debounced; /* last state */
  bool candidate; /* current state */
  uint16_t since; /* tick_us() since last change */
};

static struct btn_state btn_state[BTN_COUNT];

/*
 * Initialises button and prepares for use.
 */
bool buttons_init(void) {
  LL_AHB1_GRP1_EnableClock(NAV_BTN_IO_CLK);

  LL_GPIO_InitTypeDef gpio = {.Pin = NAV_BTN_N_PIN | NAV_BTN_E_PIN |
                                     NAV_BTN_S_PIN | NAV_BTN_W_PIN |
                                     NAV_BTN_C_PIN,
                              .Mode = LL_GPIO_MODE_INPUT,
                              .Pull = LL_GPIO_PULL_UP};

  if (LL_GPIO_Init(NAV_BTN_PORT, &gpio) != SUCCESS) {
    ERROR_LOG("Unable to initialise NAV_BTN IO");
    return false;
  }

  for (enum btn_id id = 0; id < BTN_COUNT; id++) {
    btn_state[id].debounced = false;
    btn_state[id].candidate = false;
    btn_state[id].since = tick_us();
  }

  return true;
}

/*
 * Polling approach, so audio interrupt is the only interrupt in the system.
 */
#define BTN_FIFO_SIZE (16)

static struct btn_event btn_fifo[BTN_FIFO_SIZE];
static uint8_t btn_fifo_head = 0;
static uint8_t btn_fifo_tail = 0;

static void btn_fifo_push(struct btn_event event) {
  uint8_t next = (uint8_t)((btn_fifo_head + 1) % BTN_FIFO_SIZE);

  if (next == btn_fifo_tail) {
    RTT_LOG("Button buffer overflow");
    return; /* full - ui_routine() isn't draining fast enough */
  }

  btn_fifo[btn_fifo_head] = event;
  btn_fifo_head = next;
}

/*
 * Read the button values, software debounce, and enqueue.
 */
void buttons_service(void) {
  uint16_t now = tick_us();

  for (enum btn_id id = 0; id < BTN_COUNT; id++) {
    struct btn_state *state = &btn_state[id];

    /* Active-low */
    bool raw_pressed = !LL_GPIO_IsInputPinSet(NAV_BTN_PORT, btn_pin[id]);

    if (raw_pressed != state->candidate) {
      state->candidate = raw_pressed;
      state->since = now;
      continue;
    }

    if (state->candidate == state->debounced) {
      continue; /* done */
    }

    if ((uint16_t)(now - state->since) >= NAV_BTN_DEBOUNCE_US) {
      state->debounced = state->candidate;
      btn_fifo_push((struct btn_event){.id = id, .pressed = state->debounced});
    }
  }
}

/*
 * Poll to fetch button press events from the FIFO, called by the superloop.
 */
bool buttons_poll_event(struct btn_event *event) {
  if (btn_fifo_tail == btn_fifo_head) {
    return false; /* empty */
  }

  *event = btn_fifo[btn_fifo_tail];
  btn_fifo_tail = (uint8_t)((btn_fifo_tail + 1) % BTN_FIFO_SIZE);

  return true;
}
