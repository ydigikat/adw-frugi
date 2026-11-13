/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
*/
#ifndef FRUGI_EEPROM_H
#define FRUGI_EEPROM_H

#include <stdbool.h>
#include <stdint.h>

#include "spi1_bus.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_gpio.h"

/*
 * ------------------------------------------------------------------
 * 25LC256 EEPROM - shares SPI1 with others with own CS.
 * ------------------------------------------------------------------
 */

#define EEPROM_SPI (SPI1_BUS_SPI)

/* Control pins */
#define EEPROM_CTRL_IO_CLK (LL_AHB1_GRP1_PERIPH_GPIOA)
#define EEPROM_CTRL_PORT (GPIOA)
#define EEPROM_CS_PIN (LL_GPIO_PIN_0)

#define EEPROM_CS_LOW()                                                        \
  (LL_GPIO_ResetOutputPin(EEPROM_CTRL_PORT, EEPROM_CS_PIN))
#define EEPROM_CS_HIGH() (LL_GPIO_SetOutputPin(EEPROM_CTRL_PORT, EEPROM_CS_PIN))

/* 25LC256 geometry */
#define EEPROM_SIZE_BYTES (32768u)
#define EEPROM_PAGE_SIZE (64u)

/* API - all calls are blocking */
bool eeprom_init(void);
bool eeprom_write(uint16_t addr, const uint8_t *data, uint16_t len);
bool eeprom_read(uint16_t addr, uint8_t *dst, uint16_t len);

#endif /* FRUGI_EEPROM_H */
