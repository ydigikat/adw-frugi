/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   25LC256 SPI EEPROM driver.

   All operations are blocking.  Reads are fast (~200us for a 256 byte patch)
   and writes are dominated by the chip's internal write cycle (up to 5ms per
   64 byte page), so DMA would add complexity for no gain.  Patch
   save/load is a non-performance action and, like most hardware synths, it is
   acceptable for it to briefly halt the rest of the system.
   -----------------------------------------------------------------------------
*/
#include "eeprom.h"
#include "board.h"
#include "stm32f4xx_ll_spi.h"
#include "trace.h"

#define EEPROM_CMD_READ (0x03)
#define EEPROM_CMD_WRITE (0x02)
#define EEPROM_CMD_WREN (0x06)
#define EEPROM_CMD_RDSR (0x05)
#define EEPROM_SR_WIP (0x01)

/*
 * Internal write cycle is 5ms max (datasheet tWC).  Allow double that before
 * declaring the chip unresponsive.
 */
#define EEPROM_WIP_POLL_US (100u)
#define EEPROM_WIP_TIMEOUT_POLLS (100u)

/*
 * Peripheral initialisation: control pins.  Other peripherals may be on SPI1
 * however SPI init is idempotent so any/all can call spi1_bus_init().
 */
bool eeprom_init(void) {
  if (!spi1_bus_init()) {
    return false;
  }

  LL_AHB1_GRP1_EnableClock(EEPROM_CTRL_IO_CLK);

  LL_GPIO_InitTypeDef ctrl_io = {.Pin = EEPROM_CS_PIN,
                                 .Mode = LL_GPIO_MODE_OUTPUT,
                                 .OutputType = LL_GPIO_OUTPUT_PUSHPULL,
                                 .Speed = LL_GPIO_SPEED_FREQ_HIGH,
                                 .Pull = LL_GPIO_PULL_NO};

  if (LL_GPIO_Init(EEPROM_CTRL_PORT, &ctrl_io) != SUCCESS) {
    ERROR_LOG("Unable to initialise EEPROM control pins.");
    return false;
  }

  EEPROM_CS_HIGH();

  return true;
}

/*
 * Start a transaction.  The bus is shared so wait for any other device's
 * DMA burst to finish - once spi1_bus_busy() returns false that device has
 * also been deselected.
 *
 * The bus default clock is set for the LCD and is too fast for the 25LC256
 * (5MHz max at 3.3V), so drop to the EEPROM rate while both chips are
 * deselected, before CS goes low.
 *
 * Finally discard any stale received data.  Without the flush, a byte left
 * in the receive register by an earlier transmit-only transfer makes every
 * exchange below return one byte late.
 */
static void eeprom_select(void) {
  while (spi1_bus_busy())
    ;

  spi1_bus_set_prescaler(SPI1_BUS_PRESCALER_EEPROM);
  spi1_bus_flush_rx();
  EEPROM_CS_LOW();
}

/*
 * End a transaction.  CS must not rise until the last bit has left the shift
 * register, otherwise the chip aborts the command.  The bus clock is then
 * restored for the other devices.
 *
 * Every EEPROM operation runs start to finish inside one blocking call, so
 * no other device can use the bus while it is at the slower rate.
 */
static void eeprom_deselect(void) {
  while (LL_SPI_IsActiveFlag_BSY(EEPROM_SPI))
    ;

  EEPROM_CS_HIGH();
  spi1_bus_set_prescaler(SPI1_BUS_PRESCALER_DEFAULT);
}

/*
 * Full-duplex single byte exchange.  SPI always receives as it transmits so
 * every byte sent is matched by reading one back, keeping the receive
 * register in step.
 */
static uint8_t eeprom_xfer_byte(uint8_t byte) {
  while (!LL_SPI_IsActiveFlag_TXE(EEPROM_SPI))
    ;

  LL_SPI_TransmitData8(EEPROM_SPI, byte);

  while (!LL_SPI_IsActiveFlag_RXNE(EEPROM_SPI))
    ;

  return LL_SPI_ReceiveData8(EEPROM_SPI);
}

/*
 * Opcode followed by 16-bit address, MSB first.
 */
static void eeprom_send_cmd_addr(uint8_t cmd, uint16_t addr) {
  eeprom_xfer_byte(cmd);
  eeprom_xfer_byte((uint8_t)(addr >> 8));
  eeprom_xfer_byte((uint8_t)(addr & 0xFF));
}

/*
 * Set the write enable latch.  The chip clears this after every write so it
 * must be sent before each page.
 */
static void eeprom_write_enable(void) {
  eeprom_select();
  eeprom_xfer_byte(EEPROM_CMD_WREN);
  eeprom_deselect();
}

static uint8_t eeprom_read_status(void) {
  eeprom_select();
  eeprom_xfer_byte(EEPROM_CMD_RDSR);
  uint8_t status = eeprom_xfer_byte(0x00);
  eeprom_deselect();

  return status;
}

/*
 * Block until the chip finishes its internal write cycle.  It ignores all
 * other commands while the WIP (write in progress) bit is set.
 */
static bool eeprom_wait_write_complete(void) {
  for (uint32_t i = 0; i < EEPROM_WIP_TIMEOUT_POLLS; i++) {
    if (!(eeprom_read_status() & EEPROM_SR_WIP)) {
      return true;
    }
    delay_us(EEPROM_WIP_POLL_US);
  }

  ERROR_LOG("EEPROM write cycle timed out.");
  return false;
}

/*
 * Write within a single page.  The chip wraps to the start of the same page
 * rather than spilling into the next, so the caller must not cross a page
 * boundary.
 */
static bool eeprom_write_page(uint16_t addr, const uint8_t *data,
                              uint16_t len) {
  eeprom_write_enable();

  eeprom_select();
  eeprom_send_cmd_addr(EEPROM_CMD_WRITE, addr);
  for (uint16_t i = 0; i < len; i++) {
    eeprom_xfer_byte(data[i]);
  }
  eeprom_deselect();

  return eeprom_wait_write_complete();
}

/*
 * Blocking write of len bytes starting at addr.  Split into page-sized
 * writes so any address and length can be used.
 */
bool eeprom_write(uint16_t addr, const uint8_t *data, uint16_t len) {
  if (data == NULL || len == 0 ||
      (uint32_t)addr + len > EEPROM_SIZE_BYTES) {
    ERROR_LOG("eeprom_write: invalid address range.");
    return false;
  }

  while (len > 0) {
    uint16_t page_remaining =
        (uint16_t)(EEPROM_PAGE_SIZE - (addr % EEPROM_PAGE_SIZE));
    uint16_t chunk = len < page_remaining ? len : page_remaining;

    if (!eeprom_write_page(addr, data, chunk)) {
      return false;
    }

    addr = (uint16_t)(addr + chunk);
    data += chunk;
    len = (uint16_t)(len - chunk);
  }

  return true;
}

/*
 * Blocking read of len bytes starting at addr.  Unlike writes, reads stream
 * continuously across page boundaries.
 */
bool eeprom_read(uint16_t addr, uint8_t *dst, uint16_t len) {
  if (dst == NULL || len == 0 || (uint32_t)addr + len > EEPROM_SIZE_BYTES) {
    ERROR_LOG("eeprom_read: invalid address range.");
    return false;
  }

  eeprom_select();
  eeprom_send_cmd_addr(EEPROM_CMD_READ, addr);
  for (uint16_t i = 0; i < len; i++) {
    dst[i] = eeprom_xfer_byte(0x00);
  }
  eeprom_deselect();

  return true;
}
