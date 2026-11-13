/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   The UART uses DMA to transfer serial data into a circular buffer, no ISR is
   used.

   The buffer is polled (and drained) by the MIDI routine in the application 
   before the start of each audio block.  

   Runing at 48kHz with a block size of 64 (or 128) the period only allows
   a small number of MIDI bytes to accumulate, even with bursts from a
   sequencer, so the buffer does not need to be large and there is no musical
   impact caused by it.

   The one time the superloop deliberately stops draining it is a patch save,
   which blocks for ~20ms.  At the full wire rate (3125 bytes/s) that is ~62
   bytes, so the buffer is sized at 128 to absorb the stall - a sequencer may
   still be sending clock or active sensing.  The circular DMA cannot report
   an overflow, it silently overwrites unread bytes, so headroom is the only
   protection.
   -----------------------------------------------------------------------------
*/
#include "midi_uart.h"
#include "board.h"

#include "stm32f4xx_ll_usart.h"

/*
 * The USART DMA writes incoming serial bytes directly into this circular
 * buffer, which midi_poll_byte() then drains.
 */
#ifndef MIDI_DMA_BUFFER_SIZE
#define MIDI_DMA_BUFFER_SIZE (128)
#endif

static uint8_t midi_rx_dma_buffer[MIDI_DMA_BUFFER_SIZE];
static uint32_t last_dma_pos = 0;

/*
 * Bring up the UART/DMA and start MIDI running.
 */
bool midi_start(void) {
  LL_APB2_GRP1_EnableClock(MIDI_UART_PERIPH_CLK);
  LL_AHB1_GRP1_EnableClock(MIDI_UART_IO_CLK);
  LL_AHB1_GRP1_EnableClock(MIDI_UART_DMA_CLK);

  LL_GPIO_InitTypeDef gpio = {.Mode = LL_GPIO_MODE_ALTERNATE,
                              .Alternate = MIDI_UART_AF,
                              .Pin = MIDI_UART_RX_PIN,
                              .Speed = LL_GPIO_SPEED_FREQ_HIGH,
                              .Pull = LL_GPIO_PULL_UP};

  if (LL_GPIO_Init(MIDI_UART_RX_PORT, &gpio) != SUCCESS) {
    ERROR_LOG("Unable to initialise UART pins.");
    return false;
  }

  /* USART config: 31250, 8N1 RX only */
  LL_USART_InitTypeDef usart = {.BaudRate = 31250,
                                .OverSampling = LL_USART_OVERSAMPLING_16,
                                .DataWidth = LL_USART_DATAWIDTH_8B,
                                .Parity = LL_USART_PARITY_NONE,
                                .StopBits = LL_USART_STOPBITS_1,
                                .TransferDirection = LL_USART_DIRECTION_RX};

  if (LL_USART_Init(MIDI_UART, &usart) != SUCCESS) {
    ERROR_LOG("Unable to initialise UART");
    return false;
  }

  LL_DMA_InitTypeDef dma_uart_rx = {
      .PeriphOrM2MSrcAddress = (uint32_t)&MIDI_UART->DR,
      .MemoryOrM2MDstAddress = (uint32_t)midi_rx_dma_buffer,
      .Direction = LL_DMA_DIRECTION_PERIPH_TO_MEMORY,
      .Mode = LL_DMA_MODE_CIRCULAR,
      .PeriphOrM2MSrcIncMode = LL_DMA_PERIPH_NOINCREMENT,
      .MemoryOrM2MDstIncMode = LL_DMA_MEMORY_INCREMENT,
      .PeriphOrM2MSrcDataSize = LL_DMA_PDATAALIGN_BYTE,
      .MemoryOrM2MDstDataSize = LL_DMA_MDATAALIGN_BYTE,
      .NbData = MIDI_DMA_BUFFER_SIZE,
      .Channel = MIDI_UART_DMA_CHANNEL,
      .Priority = LL_DMA_PRIORITY_HIGH,
      .FIFOMode = LL_DMA_FIFOMODE_DISABLE};

  if (LL_DMA_Init(MIDI_UART_DMA, MIDI_UART_DMA_STREAM, &dma_uart_rx) !=
      SUCCESS) {
    ERROR_LOG("Unable to initialise UART DMA.");
    return false;
  }

  LL_USART_EnableDMAReq_RX(MIDI_UART);

  LL_DMA_EnableStream(MIDI_UART_DMA, MIDI_UART_DMA_STREAM);
  while (!LL_DMA_IsEnabledStream(MIDI_UART_DMA, MIDI_UART_DMA_STREAM))
    ;

  LL_USART_Enable(MIDI_UART);
  while (!(LL_USART_IsEnabled(MIDI_UART)))
    ;

  return true;
}

/*
 * Called by the MIDI task to retrieve bytes from the DMA MIDI RX buffer.
 */
bool midi_poll_byte(uint8_t *byte) {
  /* Get current DMA write position */
  uint32_t dma_pos = MIDI_DMA_BUFFER_SIZE -
                     LL_DMA_GetDataLength(MIDI_UART_DMA, MIDI_UART_DMA_STREAM);

  /* Check if new data is available */
  if (last_dma_pos == dma_pos) {
    return false; /* Buffer empty */
  }

  /* Read next byte from circular buffer */
  *byte = midi_rx_dma_buffer[last_dma_pos];
  last_dma_pos = (last_dma_pos + 1) % MIDI_DMA_BUFFER_SIZE;

  return true;
}

/*
 * Returns the number of pending bytes in the MIDI RX buffer.
 */
uint32_t midi_pending_bytes(void) {
  uint32_t dma_pos = MIDI_DMA_BUFFER_SIZE -
                     LL_DMA_GetDataLength(MIDI_UART_DMA, MIDI_UART_DMA_STREAM);
  return (dma_pos + MIDI_DMA_BUFFER_SIZE - last_dma_pos) % MIDI_DMA_BUFFER_SIZE;
}
