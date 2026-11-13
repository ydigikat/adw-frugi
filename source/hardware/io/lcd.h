/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
*/
#ifndef FRUGI_LCD_H
#define FRUGI_LCD_H

#include <stdbool.h>
#include <stdint.h>

#include "stm32f4xx.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_dma.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_spi.h"

#include "spi1_bus.h"

/*
 * ------------------------------------------------------------------
 * Peripheral/pin usage - shares SPI with others with own CS.
 * ------------------------------------------------------------------
 * SPI1 (LCD + EEPROM)   SCK=PA5  MISO=PA6  MOSI=PA7
 * LCD  CS                                  PA4
 * LCD  DC (command/data)                   PA1
 * LCD  RST                                 PA2
 * ------------------------------------------------------------------
 */

#define LCD_SPI (SPI1_BUS_SPI)

/*
 * Panel resolution, landscape (after the MADCTL rotation set in
 * lcd_chip_init()). 240x320 is the ILI9341's native portrait size -
 * landscape swaps that to 320x240.
 */
#define LCD_PX_WIDTH (320)
#define LCD_PX_HEIGHT (240)

/* Control pins */
#define LCD_CTRL_PORT (GPIOA)
#define LCD_CS_PIN (LL_GPIO_PIN_4)
#define LCD_DC_PIN (LL_GPIO_PIN_1)
#define LCD_RST_PIN (LL_GPIO_PIN_2)

#define LCD_CS_LOW() (LL_GPIO_ResetOutputPin(LCD_CTRL_PORT, LCD_CS_PIN))
#define LCD_CS_HIGH() (LL_GPIO_SetOutputPin(LCD_CTRL_PORT, LCD_CS_PIN))
#define LCD_DC_COMMAND() (LL_GPIO_ResetOutputPin(LCD_CTRL_PORT, LCD_DC_PIN))
#define LCD_DC_DATA() (LL_GPIO_SetOutputPin(LCD_CTRL_PORT, LCD_DC_PIN))
#define LCD_RST_LOW() (LL_GPIO_ResetOutputPin(LCD_CTRL_PORT, LCD_RST_PIN))
#define LCD_RST_HIGH() (LL_GPIO_SetOutputPin(LCD_CTRL_PORT, LCD_RST_PIN))

/* API */
bool lcd_init(void);
/*
 * pixels is mutated in place - each RGB565 value is byte-swapped to the
 * big-endian order the controller expects on the wire before the DMA
 * burst goes out (SPI1 is configured byte-wise, so a plain memory-order
 * send of a little-endian uint16_t buffer comes out LSB-first, the wrong
 * way round). Safe because this is always display.c's single-use row
 * scratch buffer - nothing re-reads it before the next render overwrites
 * it. Data must stay valid for the duration of lcd_write_busy().
 */
bool lcd_write_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                      uint16_t *pixels);
bool lcd_write_busy(void);

#endif /* FRUGI_LCD_H */
