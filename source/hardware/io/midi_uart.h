/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
*/
#ifndef FRUGI_MIDI_UART_H
#define FRUGI_MIDI_UART_H

#include <stdbool.h>
#include <stdint.h>

#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_dma.h"
#include "stm32f4xx_ll_gpio.h"


/*
 * ------------------------------------------------------------------
 * MIDI-in: UART1 RX, DMA2 Stream2/Channel4.
 * ------------------------------------------------------------------
 */

#define MIDI_UART_PERIPH_CLK (LL_APB2_GRP1_PERIPH_USART1)
#define MIDI_UART_IO_CLK (LL_AHB1_GRP1_PERIPH_GPIOA)
#define MIDI_UART_DMA_CLK (LL_AHB1_GRP1_PERIPH_DMA2)
#define MIDI_UART (USART1)
#define MIDI_UART_AF (LL_GPIO_AF_7)
#define MIDI_UART_RX_PIN (LL_GPIO_PIN_10)
#define MIDI_UART_RX_PORT (GPIOA)
#define MIDI_UART_DMA (DMA2)
#define MIDI_UART_DMA_STREAM (LL_DMA_STREAM_2)
#define MIDI_UART_DMA_CHANNEL (LL_DMA_CHANNEL_4)

/* API */
bool midi_start(void);
bool midi_poll_byte(uint8_t *byte);
uint32_t midi_pending_bytes(void);

#endif /* FRUGI_MIDI_UART_H */
