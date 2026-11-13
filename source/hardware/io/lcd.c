/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   The LCD sends data through DMA and SPI.  The SPI bus is shared with other
   devices so checks are needed that the SPI BUS is not already busy.  The
   bus deselects the LCD when a burst completes (see spi1_bus.h).

   The LCD runs at the bus default clock (12.5MHz) - redraw speed depends on
   it.  Slower devices drop the clock only for their own transactions.
   
   Unlike I2S or UART this uses a single-shot DMA burst to transfer data to the
   GRAM in the LCD. Commands and data are both sent the same way, the D/C line
   being used to signal which.

   The LCD interface is only concerned with transferring data, it does not
   provide any graphics, these come from the display layer above in the app.
   -----------------------------------------------------------------------------
*/
#include <stdbool.h>
#include <stdint.h>

#include "board.h"
#include "lcd.h"

static bool lcd_chip_init(void);

/*
 * Peripheral initialisation
 */
bool lcd_init(void) {
  if (!spi1_bus_init()) {
    return false;
  }

  LL_GPIO_InitTypeDef ctrl_io = {.Mode = LL_GPIO_MODE_OUTPUT,
                                 .OutputType = LL_GPIO_OUTPUT_PUSHPULL,
                                 .Speed = LL_GPIO_SPEED_FREQ_HIGH,
                                 .Pull = LL_GPIO_PULL_NO};

  ctrl_io.Pin = LCD_CS_PIN | LCD_RST_PIN | LCD_DC_PIN;
  if (LL_GPIO_Init(LCD_CTRL_PORT, &ctrl_io) != SUCCESS) {
    ERROR_LOG("Unable to initialise LCD control pins");
    return false;
  }

  LCD_CS_HIGH();
  LCD_DC_DATA();
  LCD_RST_HIGH();

  return lcd_chip_init();
}

/*
 * Polling - true while a window write's DMA burst is still on the bus.
 * The bus raises our CS itself when the burst completes (see
 * lcd_dma_release()), so there is no LCD-side state to tidy up here.
 */
bool lcd_write_busy(void) { return spi1_bus_busy(); }

/*
 * Burst release callback, called by spi1_bus_busy() once the last pixel has
 * left the wire.  Deselecting here, rather than waiting for the LCD code to
 * poll, means another device on the bus can never find the LCD still
 * selected.
 */
static void lcd_dma_release(void) { LCD_CS_HIGH(); }

/*
 * Write a one-shot burst of data to the LCD.  The caller has already
 * checked the bus is free and asserted CS.
 */
static bool lcd_dma_write(const uint8_t *data, uint32_t len) {
  spi1_bus_start_dma(data, len, lcd_dma_release);

  return true;
}

/*
 * IL9341 Chip driver
 */

#define ILI9341_SWRESET (0x01)
#define ILI9341_SLPOUT (0x11)
#define ILI9341_DISPON (0x29)
#define ILI9341_CASET (0x2A)
#define ILI9341_PASET (0x2B)
#define ILI9341_RAMWR (0x2C)
#define ILI9341_MADCTL (0x36) /* memory access control - orientation */
#define ILI9341_PIXFMT (0x3A) /* interface pixel format */

/*
 * Write a byte to the ILI9341, this is either a command
 * or a data byte.  The D/C line is set accordingly.
 */
inline static void lcd_write(uint8_t byte, bool is_cmd) {

  LCD_CS_LOW();
  delay_us(2);

  is_cmd ? LCD_DC_COMMAND() : LCD_DC_DATA();

  while (!LL_SPI_IsActiveFlag_TXE(LCD_SPI))
    ;

  LL_SPI_TransmitData8(LCD_SPI, byte);

  while (LL_SPI_IsActiveFlag_BSY(LCD_SPI))
    ;

  /*
   * BSY clearing doesn't mean the last bit has finished propagating onto
   * the physical SCK/MOSI lines - delay before CS
  */
  delay_us(2);

  LCD_CS_HIGH();
  delay_us(2);
}

static void lcd_write_cmd(uint8_t cmd) { lcd_write(cmd, true); }

static void lcd_write_data8(uint8_t data) { lcd_write(data, false); }

static void lcd_write_data16(uint16_t data) {
  lcd_write_data8((uint8_t)(data >> 8));
  lcd_write_data8((uint8_t)(data & 0xFF));
}

/*
 * Fills the whole physical panel black. This is fast enough that we don't 
 * need to propagate any kind of yield flag.  This is the only 'graphical'
 * type operation in the hardware layer and is to ensure the panel is cleared
 * correctly at initialisation.
 */
static void lcd_clear_screen(void) {
  static uint16_t black_row[LCD_PX_WIDTH];

  for (uint16_t y = 0; y < LCD_PX_HEIGHT; y++) {
    while (lcd_write_busy())
      ;

    lcd_write_window(0, y, LCD_PX_WIDTH, 1, black_row);
  }

  while (lcd_write_busy())
    ;
}

/* Delays in this init are from datasheet  */
static bool lcd_chip_init(void) {
  LCD_RST_LOW();
  delay_ms(20);
  LCD_RST_HIGH();
  delay_ms(120);

  lcd_write_cmd(ILI9341_SWRESET);
  delay_ms(120);

  lcd_write_cmd(ILI9341_SLPOUT);
  delay_ms(120);

  /* 16-bits per pixel */
  lcd_write_cmd(ILI9341_PIXFMT);
  lcd_write_data8(0x55); 

  /* Landscape */
  lcd_write_cmd(ILI9341_MADCTL);  
  lcd_write_data8(0xE8);

  lcd_write_cmd(ILI9341_DISPON);
  delay_ms(120);

  /*
   * The panel's ID (RDDID4, 0xD3) is not read back to confirm an ILI9341 is
   * fitted: reads need a slower SPI clock than writes and some modules don't
   * wire the panel's SDO at all.  A missing or wrong panel is obvious at
   * power-up anyway.
   */

  lcd_clear_screen();

  return true;
}

/*
 * Set the controller's address window to (x, y, w, h) and send data.
 * Note that data must stay valid for duration of lcd_write_busy() because
 * it is used directly by DMA not a copy.
 */
bool lcd_write_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                      uint16_t *pixels) {
  if (lcd_write_busy()) {
    return false;
  }

  lcd_write_cmd(ILI9341_CASET);
  lcd_write_data16(x);
  lcd_write_data16((uint16_t)(x + w - 1));

  lcd_write_cmd(ILI9341_PASET);
  lcd_write_data16(y);
  lcd_write_data16((uint16_t)(y + h - 1));

  lcd_write_cmd(ILI9341_RAMWR);

  /*
   * SPI1 is byte-wide and DMA transfers data in memory order.  This means
   * on the STM32F4 which is little-endian, the pixels (RGB565) will go out
   * backwards (LSB first). To fix this the bytes are flipped in place.  
   * This is easier than the alternative of switching between 8/16 bit packets
   * for cmd/data.   
   */
  uint32_t pixel_count = (uint32_t)w * h;
  for (uint32_t i = 0; i < pixel_count; i++) {
    pixels[i] = (uint16_t)((pixels[i] << 8) | (pixels[i] >> 8));
  }

  LCD_CS_LOW();
  LCD_DC_DATA();

  /* Trigger one-shot DMA transfer */
  return lcd_dma_write((const uint8_t *)pixels, pixel_count * sizeof(uint16_t));
}
