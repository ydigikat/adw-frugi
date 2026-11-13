/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
*/
#ifndef FRUGI_I2S_H
#define FRUGI_I2S_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_dma.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_rcc.h"



/* I2S */
#define I2S_PLL_M (LL_RCC_PLLI2SM_DIV_25)
#define I2S_IO_CLK (LL_AHB1_GRP1_PERIPH_GPIOB)
#define I2S_DMA_CLK (LL_AHB1_GRP1_PERIPH_DMA1)
#define I2S_PERIPH_CLK (LL_APB1_GRP1_PERIPH_SPI2)
#define I2S (SPI2)
#define I2S_DMA (DMA1)
#define I2S_PORT (GPIOB)
#define I2S_AF (LL_GPIO_AF_5)
#define I2S_SCK_PIN (LL_GPIO_PIN_10)
#define I2S_SDO_PIN (LL_GPIO_PIN_15)
#define I2S_WS_PIN (LL_GPIO_PIN_12)
#define I2S_DMA_STREAM (LL_DMA_STREAM_4)
#define I2S_DMA_CHANNEL (LL_DMA_CHANNEL_0)
#define I2S_DMA_IRQN (DMA1_Stream4_IRQn)
#define I2S_DMA_IRQ_HANDLER DMA1_Stream4_IRQHandler
#define I2S_DMA_HISR_TCIF (DMA_HISR_TCIF4)
#define I2S_DMA_HIFCR_CTCIF (DMA_HIFCR_CTCIF4)
#define I2S_DMA_HIFCR_CHTIF (DMA_HIFCR_CHTIF4)
#define I2S_DMA_HISR_HTIF (DMA_HISR_HTIF4)
#define I2S_DMA_HISR_TEIF (DMA_HISR_TEIF4)
#define I2S_DMA_HIFCR_CTEIF (DMA_HIFCR_CTEIF4)

bool audio_start(int16_t audio_buffer[], size_t len,
                 void (*fill_buffer_cb)(uint8_t buffer_idx));

#endif /* FRUGI_I2S_H*/