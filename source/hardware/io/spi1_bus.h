/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
*/
#ifndef FRUGI_SPI1_BUS_H
#define FRUGI_SPI1_BUS_H

#include <stdbool.h>
#include <stdint.h>

#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_dma.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_spi.h"

/*
 * ------------------------------------------------------------------
 * SPI1 (PA5/PA6/PA7)
 *
 * Shared by multiple devices. Only a single DMA engine (DMA2 S3/C3)
 * which may still be streaming a burst from a previous caller.
 *
 * Bus ownership rule: before asserting its own CS, a device must poll
 * spi1_bus_busy() until it returns false.  When a DMA burst finishes,
 * spi1_bus_busy() calls the release function the burst was started with,
 * which raises that device's CS.  So "bus not busy" always means "no other
 * device is still selected".
 * ------------------------------------------------------------------
 */

#define SPI1_BUS_IO_CLK (LL_AHB1_GRP1_PERIPH_GPIOA)
#define SPI1_BUS_PERIPH_CLK (LL_APB2_GRP1_PERIPH_SPI1)
#define SPI1_BUS_DMA_CLK (LL_AHB1_GRP1_PERIPH_DMA2)
#define SPI1_BUS_SPI (SPI1)
#define SPI1_BUS_PORT (GPIOA)
#define SPI1_BUS_AF (LL_GPIO_AF_5)
#define SPI1_BUS_SCK_PIN (LL_GPIO_PIN_5)
#define SPI1_BUS_MISO_PIN (LL_GPIO_PIN_6)
#define SPI1_BUS_MOSI_PIN (LL_GPIO_PIN_7)

/* SPI1 DMA (TX) is DMA2 Stream 3, Channel 3 */
#define SPI1_BUS_DMA (DMA2)
#define SPI1_BUS_DMA_STREAM (LL_DMA_STREAM_3)
#define SPI1_BUS_DMA_CHANNEL (LL_DMA_CHANNEL_3)
#define SPI1_BUS_DMA_LISR_TCIF (DMA_LISR_TCIF3)
#define SPI1_BUS_DMA_LIFCR_CTCIF (DMA_LIFCR_CTCIF3)

/*
 * Bus clock (SPI1 is on APB2, 100MHz).  The devices on this bus have
 * different maximum clock rates, so the bus runs at the LCD's rate by
 * default and a slower device switches the rate for the duration of its
 * own transaction (see spi1_bus_set_prescaler()).
 *
 * DEFAULT - 12.5MHz for the ILI9341 LCD.  A full-width text row is ~14.7KB,
 *           so redraw speed is set by this clock: ~9.4ms per row here, ~38ms
 *           at the EEPROM rate.  12.5MHz is slightly over the ILI9341's
 *           datasheet write cycle (100ns, 10MHz) - a deliberate, widely used
 *           choice; DIV16 (6.25MHz) is the strictly in-spec option.
 *
 * EEPROM  - 3.125MHz for the 25LC256, rated 5MHz max at 2.5-4.5V (10MHz only
 *           at 4.5-5.5V).  DIV16 would give 6.25MHz, still too fast.
 */
#define SPI1_BUS_PRESCALER_DEFAULT (LL_SPI_BAUDRATEPRESCALER_DIV8)
#define SPI1_BUS_PRESCALER_EEPROM (LL_SPI_BAUDRATEPRESCALER_DIV32)

/*
 * Called by spi1_bus_busy() when a DMA burst completes, to deselect (raise
 * CS on) the device that owned the burst.
 */
typedef void (*spi1_bus_release_fn)(void);

/* API */
bool spi1_bus_init(void);
bool spi1_bus_busy(void);
void spi1_bus_start_dma(const uint8_t *data, uint32_t len,
                        spi1_bus_release_fn release);
void spi1_bus_set_prescaler(uint32_t prescaler);
void spi1_bus_flush_rx(void);

#endif /* FRUGI_SPI1_BUS_H */
