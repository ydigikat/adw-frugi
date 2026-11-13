/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   I2S Stereo audio output to DAC.  Philips standard configuration, 32@48kHz.

   I2S is fed by DMA which drives the entire timing of the synth.  The half/
   complete buffer ISRs call back into the application to obtain buffer
   refills (double-buffering).  DMA operates in circular mode so
   loops continually around the audio buffer which is conceptually divided
   into 2 halfs, PING and PONG.

   There is no MCLK since many DACs do not require this.  If you do need an MCLK
   you will need to assign it to a spare GPIO pin and the I2S PLL configuration
   must be changed (see the STM references), the BCLK scalars are altered.
   
   When used to generate an accurate MCLK, the I2S PLL can no longer generate 
   an accurate BCLK so frequency errors need to be compensated for in the
   audio generator.
   -----------------------------------------------------------------------------
*/
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "i2s.h"
#include "board.h"
#include "trace.h"

#include "stm32f4xx_ll_spi.h"

#define PING (0)
#define PONG (1)

/*
 * Address of function to callback when we want the audio buffer refilled, this
 * is passed to us in the audio_init() function call.
 */
static void (*audio_fill_buffer)(uint8_t buffer_idx);

/*
 * Configures but does not start I2S. See bsp_start_audio() call.
 * Philips standard, 32-bit@48kHz.
 */
static bool i2s_init() {
  LL_AHB1_GRP1_EnableClock(I2S_DMA_CLK);
  LL_AHB1_GRP1_EnableClock(I2S_IO_CLK);
  LL_APB1_GRP1_EnableClock(I2S_PERIPH_CLK);

  LL_GPIO_InitTypeDef io = {.Speed = LL_GPIO_SPEED_FREQ_LOW,
                            .Pull = LL_GPIO_PULL_DOWN,
                            .Mode = LL_GPIO_MODE_ALTERNATE,
                            .Alternate = I2S_AF,
                            .Pin = I2S_SCK_PIN | I2S_SDO_PIN | I2S_WS_PIN};

  if (LL_GPIO_Init(I2S_PORT, &io) != SUCCESS) {
    ERROR_LOG("Unable to initialise I2S pins.");
    return false;
  }

  if (LL_DMA_Init(I2S_DMA, I2S_DMA_STREAM,
                  &(LL_DMA_InitTypeDef){
                      .Channel = I2S_DMA_CHANNEL,
                      .Direction = LL_DMA_DIRECTION_MEMORY_TO_PERIPH,
                      .PeriphOrM2MSrcIncMode = LL_DMA_PERIPH_NOINCREMENT,
                      .MemoryOrM2MDstIncMode = LL_DMA_MEMORY_INCREMENT,
                      .PeriphOrM2MSrcDataSize = LL_DMA_PDATAALIGN_HALFWORD,
                      .MemoryOrM2MDstDataSize = LL_DMA_MDATAALIGN_HALFWORD,
                      .Mode = LL_DMA_MODE_CIRCULAR,
                      .Priority = LL_DMA_PRIORITY_HIGH,
                      .FIFOMode = LL_DMA_FIFOMODE_DISABLE}) != SUCCESS) {
    ERROR_LOG("Unable to initialise I2S DMA.");
    return false;
  }

  /*
   * 48K sample rate, values here come from the STM32F411xE reference manual
   * table 90 at the time of writing.  The 25MHz HSE is divided by 25
   * (I2S_PLL_M) to give the PLL a 1MHz input; x258 / 3 gives an 86MHz I2S
   * clock.
   *
   * No MCLK pin is routed (see the note at the top of this file).  The
   * MCLKOutput setting in LL_I2S_Init() below only chooses which divider
   * formula is used - 256 x Fs with it enabled, 64 x Fs (32-bit stereo)
   * without - and from 86MHz both give 47.991kHz.  An application that does
   * route an MCLK will need different PLL values.
   */
  LL_RCC_PLLI2S_ConfigDomain_I2S(LL_RCC_PLLSOURCE_HSE, I2S_PLL_M, 258,
                                 LL_RCC_PLLI2SR_DIV_3);

  /* Enable PLLI2S */
  LL_RCC_PLLI2S_Enable();
  while (!LL_RCC_PLLI2S_IsReady())
    ;

  /* Initialise the peripheral for Philips 32-bit protocol */
  if (LL_I2S_Init(I2S, &(LL_I2S_InitTypeDef){
                           .AudioFreq = LL_I2S_AUDIOFREQ_48K,
                           .ClockPolarity = LL_I2S_POLARITY_LOW,
                           .DataFormat = LL_I2S_DATAFORMAT_32B,
                           .MCLKOutput = LL_I2S_MCLK_OUTPUT_ENABLE,
                           .Mode = LL_I2S_MODE_MASTER_TX,
                           .Standard = LL_I2S_STANDARD_PHILIPS}) != SUCCESS) {
    ERROR_LOG("Unable to initialise I2S.");
    return false;
  }

  /* Configure DMA transfer/interrupts to I2S */
  NVIC_SetPriority(I2S_DMA_IRQN, 10);
  NVIC_EnableIRQ(I2S_DMA_IRQN);
  LL_SPI_EnableDMAReq_TX(I2S);

  /* Enable interrupts at half-transfer (HT) and transfer-complete (TC) */
  LL_DMA_EnableIT_HT(I2S_DMA, I2S_DMA_STREAM);
  LL_DMA_EnableIT_TC(I2S_DMA, I2S_DMA_STREAM);
  LL_DMA_EnableIT_TE(I2S_DMA, I2S_DMA_STREAM);

  /*
   * I2S and DMA startup are finalised in audio_start() function where
   * the user provides the buffer and callback.
   */
  return true;
}


/*
 * Start the audio subsystem. This finishes I2S/DMA configuration and starts the
 * peripherals.
 */
bool audio_start(int16_t audio_buffer[], size_t len,
                 void (*fill_buffer_cb)(uint8_t buffer_idx)) {
  RTT_ASSERT(audio_buffer);
  RTT_ASSERT(len > 0);
  RTT_ASSERT(*fill_buffer_cb);

  if(!i2s_init()) {
    ERROR_LOG("Unable to initialise I2S");
    return false;
  }

  /* Stash the buffer-fill callback function pointer for use by the DMA ISR */
  audio_fill_buffer = fill_buffer_cb;

  LL_DMA_SetDataLength(I2S_DMA, I2S_DMA_STREAM, len);
  LL_DMA_ConfigAddresses(I2S_DMA, I2S_DMA_STREAM, (uint32_t)audio_buffer,
                         LL_SPI_DMA_GetRegAddr(I2S),
                         LL_DMA_DIRECTION_MEMORY_TO_PERIPH);

  LL_I2S_Enable(I2S);
  while (!LL_I2S_IsEnabled(I2S))
    ;

  LL_DMA_EnableStream(I2S_DMA, I2S_DMA_STREAM);
  while (!LL_DMA_IsEnabledStream(I2S_DMA, I2S_DMA_STREAM))
    ;

  return true;
}

/*
 * ISR for half and complete transfer of circular buffer from
 * DMA to I2S.  These are the audio ping/pong buffers.
 */
void I2S_DMA_IRQ_HANDLER(void) {
  const uint32_t status = I2S_DMA->HISR;

  /*
   * Transfer error - a bus fault on the DMA's memory or peripheral access.
   * The hardware disables the stream, so audio has stopped: log it and
   * light the fault LED (in release the watchdog does not help here, as the
   * superloop is still running).
   */
  if (status & I2S_DMA_HISR_TEIF) {
    I2S_DMA->HIFCR = I2S_DMA_HIFCR_CTEIF;
    ERROR_LOG("I2S DMA transfer error - audio stopped");
  }

  /*
   * Each flag is handled on its own, so if both are pending (the ISR ran
   * late) neither request is lost - the audio engine counts that as an
   * underrun.
   */
  if (status & I2S_DMA_HISR_HTIF) {
    I2S_DMA->HIFCR = I2S_DMA_HIFCR_CHTIF;
    audio_fill_buffer(PING);
  }

  if (status & I2S_DMA_HISR_TCIF) {
    I2S_DMA->HIFCR = I2S_DMA_HIFCR_CTCIF;
    audio_fill_buffer(PONG);
  }
}
