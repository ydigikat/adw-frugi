/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
*/
#ifndef FRUGI_ENCODER_H
#define FRUGI_ENCODER_H

#include <stdbool.h>

#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_gpio.h"


/* Encoder timer */
#define ENC_TIM (TIM3)
#define ENC_TIM_PERIPH_CLK (LL_APB1_GRP1_PERIPH_TIM3)
#define ENC_TIM_IO_CLK (LL_AHB1_GRP1_PERIPH_GPIOB)
#define ENC_TIM_AF (LL_GPIO_AF_2)
#define ENC_TIM_PORT (GPIOB)
#define ENC_TIM_CH1_PIN (LL_GPIO_PIN_4)
#define ENC_TIM_CH2_PIN (LL_GPIO_PIN_5)

/*
* TIM3 counts between encoder clicks (detents).  X2 mode counts both the
* edges so we get 2 counts per click.
*/
#define ENC_TIM_COUNTS_PER_CLICK (2)

/*
* The event passed to the UI code, this is drained by the ui_routine()
* polling not by an ISR.  Knob events are not time-critical.
*/
enum enc_event { ENC_EVENT_UP, ENC_EVENT_DOWN };


bool enc_init(void);
void enc_service(void);
bool enc_poll_event(enum enc_event *event);

#endif /* FRUGI_ENCODER_H */
