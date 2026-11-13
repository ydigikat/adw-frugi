/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   Translate a single button's raw press/release edges into long-press and
   double-click gestures.
 
   Frugi only tracks BTN_C gestures - the other nav buttons act immediately so
   bypass this.   
   -----------------------------------------------------------------------------
*/
#include "btn_gesture.h"
#include "trace.h"

void btn_gesture_init(struct btn_gesture_state *state) {
  RTT_ASSERT(state);

  state->held = false;
  state->long_press_fired = false;
  state->awaiting_second = false;
  state->press_started_us = 0;
  state->first_click_us = 0;
}

/*
* Read the button gesture.
*/
enum btn_gesture btn_gesture_update(struct btn_gesture_state *state,
                                    enum btn_edge edge, uint32_t now_us) {
  RTT_ASSERT(state);

  if (edge == BTN_EDGE_PRESSED) {
    RTT_ASSERT(!state->held);
    state->held = true;
    state->press_started_us = now_us;

    if (state->awaiting_second &&
        (uint32_t)(now_us - state->first_click_us) <=
            BTN_GESTURE_DOUBLE_CLICK_US) {
      /* 2nd press within window - so double-click detected */
      state->awaiting_second = false;
      state->long_press_fired = true;
      return BTN_GESTURE_DOUBLE_CLICK;
    }

    state->long_press_fired = false;
    return BTN_GESTURE_NONE;
  }

  if (edge == BTN_EDGE_RELEASED) {
    RTT_ASSERT(state->held);
    state->held = false;

    if (state->long_press_fired) {      
      state->long_press_fired = false;
      return BTN_GESTURE_NONE;
    }

    /* Short click - start the window. */
    state->awaiting_second = true;
    state->first_click_us = now_us;
    return BTN_GESTURE_NONE;
  }

  /* No falling edge: check for a long-press while still held */
  if (state->held && !state->long_press_fired &&
      (uint32_t)(now_us - state->press_started_us) >=
          BTN_GESTURE_LONG_PRESS_US) {
    state->long_press_fired = true;
    state->awaiting_second = false; 
    return BTN_GESTURE_LONG_PRESS;
  }

  if (state->awaiting_second &&
      (uint32_t)(now_us - state->first_click_us) > BTN_GESTURE_DOUBLE_CLICK_US) {
      /* was just a click - no second click */
    state->awaiting_second = false; 
  }

  return BTN_GESTURE_NONE;
}
