/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   This is a shared SPI bus used by both LCD and EEPROM.  The LCD streams
   pixel data with DMA, the EEPROM uses simple blocking transfers.

   Callers must wait for spi1_bus_busy() to return false before selecting
   their device.  The bus owns releasing the CS of a finished DMA burst, so
   a device that started a burst and has not polled since cannot be left
   selected when another device takes the bus.

   The devices have different maximum clock rates, see spi1_bus.h.
   -----------------------------------------------------------------------------
*/
#include <stddef.h>

#include "spi1_bus.h"
#include "board.h"
#include "stm32f4xx_ll_spi.h"

static bool initialised = false;
static bool dma_busy = false;

/* Deselects the owner of the burst in flight - see spi1_bus_busy() */
static spi1_bus_release_fn dma_release = NULL;

/*
 * Bringup SPI1 and DMA2 Stream3/channel3.  This can be safely called multiple
 * times (idempotent).
 *
 * This bus is shared between the LCD and EEPROM.
 */
bool spi1_bus_init(void) {
  if (initialised) {
    return true;
  }

  LL_AHB1_GRP1_EnableClock(SPI1_BUS_IO_CLK);
  LL_APB2_GRP1_EnableClock(SPI1_BUS_PERIPH_CLK);
  LL_AHB1_GRP1_EnableClock(SPI1_BUS_DMA_CLK);

  LL_GPIO_InitTypeDef spi_io = {.Pin = SPI1_BUS_SCK_PIN | SPI1_BUS_MISO_PIN |
                                       SPI1_BUS_MOSI_PIN,
                                .Mode = LL_GPIO_MODE_ALTERNATE,
                                .Alternate = SPI1_BUS_AF,
                                .Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH,
                                .Pull = LL_GPIO_PULL_NO};

  if (LL_GPIO_Init(SPI1_BUS_PORT, &spi_io) != SUCCESS) {
    ERROR_LOG("Unable to initialise SPI1 bus IO pins.");
    return false;
  }

  if (LL_SPI_Init(SPI1_BUS_SPI,
                  &(LL_SPI_InitTypeDef){
                      .TransferDirection = LL_SPI_FULL_DUPLEX,
                      .Mode = LL_SPI_MODE_MASTER,
                      .DataWidth = LL_SPI_DATAWIDTH_8BIT,
                      .ClockPolarity = LL_SPI_POLARITY_LOW,
                      .ClockPhase = LL_SPI_PHASE_1EDGE,
                      .NSS = LL_SPI_NSS_SOFT,
                      .BaudRate = SPI1_BUS_PRESCALER_DEFAULT,
                      .BitOrder = LL_SPI_MSB_FIRST,
                      .CRCCalculation = LL_SPI_CRCCALCULATION_DISABLE}) !=
      SUCCESS) {
    ERROR_HALT("Unable to initialise SPI1 bus.");
  }

  LL_SPI_Enable(SPI1_BUS_SPI);

  /* One shot DMA transfer, re-armed per burst by spi1_bus_start_dma() */
  if (LL_DMA_Init(SPI1_BUS_DMA, SPI1_BUS_DMA_STREAM,
                  &(LL_DMA_InitTypeDef){
                      .Channel = SPI1_BUS_DMA_CHANNEL,
                      .Direction = LL_DMA_DIRECTION_MEMORY_TO_PERIPH,
                      .PeriphOrM2MSrcIncMode = LL_DMA_PERIPH_NOINCREMENT,
                      .MemoryOrM2MDstIncMode = LL_DMA_MEMORY_INCREMENT,
                      .PeriphOrM2MSrcDataSize = LL_DMA_PDATAALIGN_BYTE,
                      .MemoryOrM2MDstDataSize = LL_DMA_MDATAALIGN_BYTE,
                      .Mode = LL_DMA_MODE_NORMAL,
                      .Priority = LL_DMA_PRIORITY_MEDIUM,
                      .FIFOMode = LL_DMA_FIFOMODE_DISABLE}) != SUCCESS) {
    ERROR_HALT("Unable to initialise SPI1 bus DMA.");
  }

  LL_SPI_EnableDMAReq_TX(SPI1_BUS_SPI);

  initialised = true;

  return true;
}

/*
 * True while a data burst is still on the bus.
 *
 * When the burst has finished this clears the DMA completion flag, disables
 * the stream and calls the burst's release function to deselect the device
 * that owned it.  Only then is the bus reported free.
 */
bool spi1_bus_busy(void) {
  if (!dma_busy) {
    return false;
  }

  if (!(SPI1_BUS_DMA->LISR & SPI1_BUS_DMA_LISR_TCIF)) {
    return true;
  }

  /*
  * TCIF only means DMA has pushed the last byte into the SPI data register,
  * not that the shift register has finished clocking it onto the wire. CS
  * must not be raised (by the caller, right after this returns false) until
  * that's actually true, or the final byte can be clipped and the next
  * transaction on this CS can see a garbled reply.
  */
  if (LL_SPI_IsActiveFlag_BSY(SPI1_BUS_SPI)) {
    return true;
  }

  SPI1_BUS_DMA->LIFCR = SPI1_BUS_DMA_LIFCR_CTCIF;
  LL_DMA_DisableStream(SPI1_BUS_DMA, SPI1_BUS_DMA_STREAM);
  spi1_bus_flush_rx();

  if (dma_release != NULL) {
    dma_release();
    dma_release = NULL;
  }

  dma_busy = false;

  return false;
}

/*
 * Changes the bus clock rate.  The reference manual says the baud rate must
 * not be changed while communication is ongoing, so the peripheral is
 * disabled (after the last frame has left the shift register), changed and
 * re-enabled.
 *
 * Only call with the bus idle and every device deselected: SCK may glitch
 * while the peripheral is disabled, which is harmless with no CS asserted.
 */
void spi1_bus_set_prescaler(uint32_t prescaler) {
  while (!LL_SPI_IsActiveFlag_TXE(SPI1_BUS_SPI))
    ;
  while (LL_SPI_IsActiveFlag_BSY(SPI1_BUS_SPI))
    ;

  LL_SPI_Disable(SPI1_BUS_SPI);
  LL_SPI_SetBaudRatePrescaler(SPI1_BUS_SPI, prescaler);
  LL_SPI_Enable(SPI1_BUS_SPI);
}

/*
 * Discard stale received data.  SPI is full duplex, so a transmit-only DMA
 * burst still fills the receive register (and sets the overrun flag) with
 * nothing reading it.  Left there, the next byte-by-byte transfer sees RXNE
 * already set and returns one byte early.  Only call when the bus is idle.
 */
void spi1_bus_flush_rx(void) {
  while (LL_SPI_IsActiveFlag_RXNE(SPI1_BUS_SPI)) {
    (void)LL_SPI_ReceiveData8(SPI1_BUS_SPI);
  }

  LL_SPI_ClearFlag_OVR(SPI1_BUS_SPI);
}

/*
 * One-shot send. Caller must ensure the bus is free (spi1_bus_busy() returned
 * false) and that they have asserted their CS before calling this.
 *
 * release is called by spi1_bus_busy() once the burst has fully left the
 * wire, and must deselect the caller's device.
 */
void spi1_bus_start_dma(const uint8_t *data, uint32_t len,
                        spi1_bus_release_fn release) {
  dma_release = release;
  dma_busy = true;

  LL_DMA_SetDataLength(SPI1_BUS_DMA, SPI1_BUS_DMA_STREAM, len);
  LL_DMA_ConfigAddresses(SPI1_BUS_DMA, SPI1_BUS_DMA_STREAM, (uint32_t)data,
                         LL_SPI_DMA_GetRegAddr(SPI1_BUS_SPI),
                         LL_DMA_DIRECTION_MEMORY_TO_PERIPH);
  LL_DMA_EnableStream(SPI1_BUS_DMA, SPI1_BUS_DMA_STREAM);
}
