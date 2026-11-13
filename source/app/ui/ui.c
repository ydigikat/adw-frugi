/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
*/

#include <stdint.h>

#include "board.h"
#include "btn_gesture.h"
#include "buttons.h"
#include "display.h"
#include "encoder.h"
#include "lcd.h"
#include "modal_menu.h"
#include "param.h"
#include "ring_menu.h"
#include "ui.h"

/* Synth so we can access params etc for any UI*/
static struct synth *synth;

/* Audio interrupt flag so we yield on any delaying operations */
static bool volatile *audio_interrupt;

/* Holds state data associaed with the current ring page/field */
static struct ring_state ring_state;

/*
 * Which page is shown: the modeless player/performance ring menu, or the
 * modal page (utilities, patch browse/save/load). BTN_C double-click
 * toggles between them - see handle_btn_c_gesture().
 */
enum ui_screen { UI_SCREEN_PERFORM, UI_SCREEN_MODAL };
static enum ui_screen screen = UI_SCREEN_PERFORM;

/* Extends tick_us()'s 16-bit, ~65ms-wrapping hardware counter into a
   monotonic (mod 2^32) microsecond clock for btn_gesture.c, by
   accumulating the delta on every call (as main.c's heartbeat does). */
static uint16_t gesture_last_tick_us;
static uint32_t gesture_clock_us;
static struct btn_gesture_state btn_c_gesture;

static void display_flush();
static void handle_button(struct btn_event event);
static void handle_btn_c_gesture(enum btn_edge edge);
static void handle_encoder(enum enc_event event);
static void render_screen();

/*
 * Intialises the UI, thie will start the hardware services and initialise
 * the ring menu. A pointer to the audio interrupt flag is stashed so that
 * any long running activities can yield if it is signalled.
 */
bool ui_init(struct synth *s, volatile bool *audio_interrupt_flag) {
  audio_interrupt = audio_interrupt_flag;
  synth = s;

  display_clear();

  bool ret = lcd_init() && enc_init() && buttons_init();

#ifdef TRACE_UI
  RTT_LOG("Display rows=%d, cols=%d\n", DISPLAY_ROWS, DISPLAY_COLS);
#endif

  if (ret) {
    ring_init(&ring_state);
    modal_init();
    btn_gesture_init(&btn_c_gesture);
    gesture_last_tick_us = tick_us();
  }

  return ret;
}

/*
 * Called by superloop, this yields to the audio interupt it signalled except
 * for the service routines which are short-lived.
 *
 * 1 Read and enqueue any encoder press.
 * 2 Drain (proccess) queued encoder events.
 * 3 Read and enqueue any button press.
 * 4 Drain (proccess) queued button presses.
 * 5 Render the current screen
 */
void ui_routine() {
  enum enc_event enc_event;
  struct btn_event btn_event;

  /*
   * Drain the input FIFOs, yielding to audio between events.  The audio flag
   * is tested BEFORE popping: testing it after would remove an event from the
   * FIFO and then discard it unhandled.  Events left in the FIFO when audio
   * takes over are handled on the next pass.
   */
  enc_service();
  while (!(*audio_interrupt) && enc_poll_event(&enc_event)) {
    handle_encoder(enc_event);
  }

  buttons_service();
  while (!(*audio_interrupt) && buttons_poll_event(&btn_event)) {
    handle_button(btn_event);
  }

  if (!(*audio_interrupt)) {
    
    handle_btn_c_gesture(BTN_EDGE_NONE);

    /*
    * Render the ring-menu or modal screen.
    */
    render_screen();
  }

  display_flush();
}

/*
 * Handles the button presses. Only BTN_C is gesture decoded on
 * press/release (needs both edges).  The other buttons bypass
 * the gesture decoding.
 */
static void handle_button(struct btn_event event) {
  if (event.id == BTN_C) {
    handle_btn_c_gesture(event.pressed ? BTN_EDGE_PRESSED : BTN_EDGE_RELEASED);
    return;
  }

  if (!event.pressed) {
    return;
  }

  if (screen == UI_SCREEN_MODAL) {
    switch (event.id) {
    case BTN_N:
      modal_prev_field();
      break;
    case BTN_S:
      modal_next_field();
      break;
    case BTN_E:
      modal_right();
      break;
    case BTN_W:
      modal_left();
      break;
    default:
      break;
    }
    return;
  }

  switch (event.id) {
  case BTN_E:
    display_clear();
    ring_next_page(&ring_state);
    break;

  case BTN_W:
    display_clear();
    ring_prev_page(&ring_state);
    break;

  case BTN_N:
    ring_prev_field(&ring_state);
    break;

  case BTN_S:
    ring_next_field(&ring_state);
    break;

  default:
    break;
  }
}

/*
 * Feeds the BTN_C edge (or BTN_EDGE_NONE) through the gesture decoder
 * an records any event.
 */
static void handle_btn_c_gesture(enum btn_edge edge) {
  uint16_t now = tick_us();
  gesture_clock_us += (uint16_t)(now - gesture_last_tick_us);
  gesture_last_tick_us = now;

  enum btn_gesture gesture =
      btn_gesture_update(&btn_c_gesture, edge, gesture_clock_us);

  switch (gesture) {    
    /*
    * Double-click, toggle between rign and modal menus.
    */
  case BTN_GESTURE_DOUBLE_CLICK:
    screen = (screen == UI_SCREEN_PERFORM) ? UI_SCREEN_MODAL : UI_SCREEN_PERFORM;
    display_clear();

    if (screen == UI_SCREEN_MODAL) {
      modal_on_shown();
    } else {
      modal_on_hidden();
      set_param_changed(true); 
    }
    break;

  case BTN_GESTURE_LONG_PRESS:
    /* 
    * Only meaningful to modal screen (confirmation gesture)
    */
    if (screen == UI_SCREEN_MODAL) {
      param_value_t loaded[PARAM_COUNT];
      if (modal_confirm(synth->params, loaded)) {
        synth_load_patch(synth, loaded);
      }
    }
    break;

  case BTN_GESTURE_NONE:
  default:
    break;
  }
}

/*
 * Handle encoder changes, step increase/decrease field values.
 */
static void handle_encoder(enum enc_event event) {
  int8_t direction = (event == ENC_EVENT_UP) ? 1 : -1;
#ifdef TRACE_UI
  RTT_LOG("ENC: %d\n", event);
#endif

  if (screen == UI_SCREEN_MODAL) {
    modal_encoder(direction);
    return;
  }

  enum param_id id = ring_selected_param(&ring_state);
  synth_set_param(synth, id,
                  ring_adjust_value(&ring_state, synth->params[id], direction));
}

/*
 * Render the current screen.  
 */
static void render_screen() {
  if (screen == UI_SCREEN_MODAL) {
    render_modal();
  } else {
    render_ring(&ring_state, synth->params);
  }
}

/*
 * Transfers any dirty display rows to the physical LCD with a
 * yield to any audio interrupt.
 */
static void display_flush() {
  for (uint8_t row = 0; row < DISPLAY_ROWS && !(*audio_interrupt); row++) {
    if (!display_row_dirty(row)) {
      continue;
    }

    if (lcd_write_busy()) {
      return;
    }

    /*
     * Passed as pointer because lcd_write will byte swap before placing on SPI
     * wire.  Since buffer single use this is safe to do.
     */
    const uint16_t *pixels = display_render_row(row);
    lcd_write_window(0, (uint16_t)(row * DISPLAY_CELL_H), DISPLAY_ROW_PX_WIDTH,
                     DISPLAY_CELL_H, (uint16_t *)pixels);
  }
}

