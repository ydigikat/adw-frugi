/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
*/
#ifndef FRUGI_BOARD_INTERNAL_H
#define FRUGI_BOARD_INTERNAL_H

#define LED_STROBE_IO_CLK (LL_AHB1_GRP1_PERIPH_GPIOB)
#define LED_STROBE_PIN (LL_GPIO_PIN_8)
#define LED_STROBE_PORT (GPIOB)

#define LED_FAULT_IO_CLK (LL_AHB1_GRP1_PERIPH_GPIOB)
#define LED_FAULT_PIN (LL_GPIO_PIN_9)
#define LED_FAULT_PORT (GPIOB)
#define LED_FAULT_ON() (LL_GPIO_ResetOutputPin(LED_FAULT_PORT, LED_FAULT_PIN))
#define LED_FAULT_OFF() (LL_GPIO_SetOutputPin(LED_FAULT_PORT, LED_FAULT_PIN))

#define TIM_DELAY (TIM10)
#define TIM_DELAY_PERIPH_CLK (LL_APB2_GRP1_PERIPH_TIM10)

#endif /* FRUGI_BOARD_INTERNAL_H */
