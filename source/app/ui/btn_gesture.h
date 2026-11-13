/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
*/
#ifndef FRUGI_BTN_GESTURE_H
#define FRUGI_BTN_GESTURE_H

#include <stdbool.h>
#include <stdint.h>

/* hold time to trigger long-press */
#ifndef BTN_GESTURE_LONG_PRESS_US
#define BTN_GESTURE_LONG_PRESS_US (600000u)
#endif

/* max time between two clicks */
#ifndef BTN_GESTURE_DOUBLE_CLICK_US
#define BTN_GESTURE_DOUBLE_CLICK_US (400000u)
#endif

enum btn_edge { BTN_EDGE_NONE, BTN_EDGE_PRESSED, BTN_EDGE_RELEASED };

enum btn_gesture {
  BTN_GESTURE_NONE,
  BTN_GESTURE_DOUBLE_CLICK,
  BTN_GESTURE_LONG_PRESS,
};

struct btn_gesture_state {
  bool held;
  bool long_press_fired;
  bool awaiting_second;
  uint32_t press_started_us;
  uint32_t first_click_us;
};

void btn_gesture_init(struct btn_gesture_state *state);

enum btn_gesture btn_gesture_update(struct btn_gesture_state *state,
                                    enum btn_edge edge, uint32_t now_us);

#endif /* FRUGI_BTN_GESTURE_H */
