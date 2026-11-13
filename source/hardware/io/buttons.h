/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
*/
#ifndef FRUGI_BUTTONS_H
#define FRUGI_BUTTONS_H

#include <stdbool.h>
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_bus.h"

#define NAV_BTN_IO_CLK (LL_AHB1_GRP1_PERIPH_GPIOB)
#define NAV_BTN_PORT (GPIOB)
#define NAV_BTN_N_PIN (LL_GPIO_PIN_6)
#define NAV_BTN_E_PIN (LL_GPIO_PIN_7)
#define NAV_BTN_S_PIN (LL_GPIO_PIN_0)
#define NAV_BTN_W_PIN (LL_GPIO_PIN_1)
#define NAV_BTN_C_PIN (LL_GPIO_PIN_3)

/* Debounce time */
#define NAV_BTN_DEBOUNCE_US (10000)

enum btn_id { BTN_N, BTN_E, BTN_S, BTN_W, BTN_C, BTN_COUNT };

struct btn_event {
  enum btn_id id;
  bool pressed;
};

bool buttons_init(void);
void buttons_service(void);
bool buttons_poll_event(struct btn_event *event);

#endif /* FRUGI_BUTTONS_H */
