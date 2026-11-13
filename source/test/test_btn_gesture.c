/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
*/

#include "btn_gesture.h"
#include "unity.h"

static struct btn_gesture_state state;

void setUp() { btn_gesture_init(&state); }

void tearDown() {}

/* -------------------------------------------------------------------------
 * Single click: never fires anything on its own
 * ---------------------------------------------------------------------- */

void single_click_fires_nothing_even_after_the_window_expires(void) {
  TEST_ASSERT_EQUAL(BTN_GESTURE_NONE,
                    btn_gesture_update(&state, BTN_EDGE_PRESSED, 0));
  TEST_ASSERT_EQUAL(BTN_GESTURE_NONE,
                    btn_gesture_update(&state, BTN_EDGE_RELEASED, 10000));

  /* Still inside the window - idle polling must not fire early. */
  TEST_ASSERT_EQUAL(
      BTN_GESTURE_NONE,
      btn_gesture_update(&state, BTN_EDGE_NONE,
                         10000 + BTN_GESTURE_DOUBLE_CLICK_US / 2));

  /* Window now passed with no second click. */
  TEST_ASSERT_EQUAL(
      BTN_GESTURE_NONE,
      btn_gesture_update(&state, BTN_EDGE_NONE,
                         10000 + BTN_GESTURE_DOUBLE_CLICK_US + 1));
}

/* -------------------------------------------------------------------------
 * Double click
 * ---------------------------------------------------------------------- */

void second_press_within_window_fires_double_click_immediately(void) {
  btn_gesture_update(&state, BTN_EDGE_PRESSED, 0);
  btn_gesture_update(&state, BTN_EDGE_RELEASED, 10000);

  uint32_t second_press_at = 10000 + BTN_GESTURE_DOUBLE_CLICK_US / 2;

  /* Fires on the press, not the release. */
  TEST_ASSERT_EQUAL(
      BTN_GESTURE_DOUBLE_CLICK,
      btn_gesture_update(&state, BTN_EDGE_PRESSED, second_press_at));
}

void second_press_after_window_does_not_fire_double_click(void) {
  btn_gesture_update(&state, BTN_EDGE_PRESSED, 0);
  btn_gesture_update(&state, BTN_EDGE_RELEASED, 10000);

  uint32_t late_press_at = 10000 + BTN_GESTURE_DOUBLE_CLICK_US + 1;

  TEST_ASSERT_EQUAL(BTN_GESTURE_NONE,
                    btn_gesture_update(&state, BTN_EDGE_PRESSED, late_press_at));
}

void releasing_the_second_press_of_a_double_click_fires_nothing_more(void) {
  btn_gesture_update(&state, BTN_EDGE_PRESSED, 0);
  btn_gesture_update(&state, BTN_EDGE_RELEASED, 10000);
  btn_gesture_update(&state, BTN_EDGE_PRESSED, 10000 + 1000);

  TEST_ASSERT_EQUAL(
      BTN_GESTURE_NONE,
      btn_gesture_update(&state, BTN_EDGE_RELEASED, 10000 + 2000));
}

void a_third_click_after_a_double_click_starts_a_fresh_pair(void) {
  /* First pair */
  btn_gesture_update(&state, BTN_EDGE_PRESSED, 0);
  btn_gesture_update(&state, BTN_EDGE_RELEASED, 1000);
  btn_gesture_update(&state, BTN_EDGE_PRESSED, 2000); /* double-click #1 */
  btn_gesture_update(&state, BTN_EDGE_RELEASED, 3000);

  /* A lone third click must not be mistaken for a second pair. */
  btn_gesture_update(&state, BTN_EDGE_PRESSED, 4000);
  TEST_ASSERT_EQUAL(BTN_GESTURE_NONE,
                    btn_gesture_update(&state, BTN_EDGE_RELEASED, 4500));
}

/* -------------------------------------------------------------------------
 * Long press
 * ---------------------------------------------------------------------- */

void holding_past_the_threshold_fires_long_press_exactly_once(void) {
  btn_gesture_update(&state, BTN_EDGE_PRESSED, 0);

  TEST_ASSERT_EQUAL(BTN_GESTURE_NONE,
                    btn_gesture_update(&state, BTN_EDGE_NONE,
                                       BTN_GESTURE_LONG_PRESS_US - 1));

  TEST_ASSERT_EQUAL(
      BTN_GESTURE_LONG_PRESS,
      btn_gesture_update(&state, BTN_EDGE_NONE, BTN_GESTURE_LONG_PRESS_US));

  /* Still held - must not fire again. */
  TEST_ASSERT_EQUAL(
      BTN_GESTURE_NONE,
      btn_gesture_update(&state, BTN_EDGE_NONE, BTN_GESTURE_LONG_PRESS_US + 500000));
}

void releasing_after_a_long_press_fires_nothing(void) {
  btn_gesture_update(&state, BTN_EDGE_PRESSED, 0);
  btn_gesture_update(&state, BTN_EDGE_NONE, BTN_GESTURE_LONG_PRESS_US);

  TEST_ASSERT_EQUAL(
      BTN_GESTURE_NONE,
      btn_gesture_update(&state, BTN_EDGE_RELEASED, BTN_GESTURE_LONG_PRESS_US + 1000));
}

void a_late_second_press_can_still_become_its_own_long_press(void) {
  /* A click, then a second press well outside the double-click window -
     the stale pending-click state from the first click must not stop
     this later press from becoming a long-press in its own right. */
  btn_gesture_update(&state, BTN_EDGE_PRESSED, 0);
  btn_gesture_update(&state, BTN_EDGE_RELEASED, 1000);

  uint32_t second_press_at = 1000 + BTN_GESTURE_DOUBLE_CLICK_US + 1000;
  TEST_ASSERT_EQUAL(
      BTN_GESTURE_NONE,
      btn_gesture_update(&state, BTN_EDGE_PRESSED, second_press_at));

  TEST_ASSERT_EQUAL(BTN_GESTURE_LONG_PRESS,
                    btn_gesture_update(&state, BTN_EDGE_NONE,
                                       second_press_at +
                                           BTN_GESTURE_LONG_PRESS_US));
}

void quick_click_after_a_long_press_does_not_retroactively_double_click(
    void) {
  btn_gesture_update(&state, BTN_EDGE_PRESSED, 0);
  btn_gesture_update(&state, BTN_EDGE_NONE, BTN_GESTURE_LONG_PRESS_US);
  btn_gesture_update(&state, BTN_EDGE_RELEASED, BTN_GESTURE_LONG_PRESS_US + 500);

  /* A fresh quick click right after must be treated as the *first* click
     of a new pair, not a second one completing the long-held press. */
  uint32_t press_at = BTN_GESTURE_LONG_PRESS_US + 1000;
  TEST_ASSERT_EQUAL(BTN_GESTURE_NONE,
                    btn_gesture_update(&state, BTN_EDGE_PRESSED, press_at));
}

/* -------------------------------------------------------------------------
 * 32-bit clock wraparound
 * ---------------------------------------------------------------------- */

void long_press_detection_survives_a_clock_wraparound(void) {
  uint32_t press_at = (uint32_t)(0u - 1000u); /* near UINT32_MAX */
  btn_gesture_update(&state, BTN_EDGE_PRESSED, press_at);

  /* Wrapped past zero by the time the threshold is reached. */
  uint32_t now = (uint32_t)(press_at + BTN_GESTURE_LONG_PRESS_US);
  TEST_ASSERT_TRUE(now < press_at); /* sanity check: this call did wrap */

  TEST_ASSERT_EQUAL(BTN_GESTURE_LONG_PRESS,
                    btn_gesture_update(&state, BTN_EDGE_NONE, now));
}

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(single_click_fires_nothing_even_after_the_window_expires);

  RUN_TEST(second_press_within_window_fires_double_click_immediately);
  RUN_TEST(second_press_after_window_does_not_fire_double_click);
  RUN_TEST(releasing_the_second_press_of_a_double_click_fires_nothing_more);
  RUN_TEST(a_third_click_after_a_double_click_starts_a_fresh_pair);

  RUN_TEST(holding_past_the_threshold_fires_long_press_exactly_once);
  RUN_TEST(releasing_after_a_long_press_fires_nothing);
  RUN_TEST(a_late_second_press_can_still_become_its_own_long_press);
  RUN_TEST(quick_click_after_a_long_press_does_not_retroactively_double_click);

  RUN_TEST(long_press_detection_survives_a_clock_wraparound);

  return UNITY_END();
}
