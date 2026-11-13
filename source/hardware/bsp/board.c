/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   Initialise the basic board.  This does not configure any peripherals which 
   are done elsewhere.

   This is concerned only with basic bring-up of the clocks, FPU and any other
   board-specific (non-optional) hardware used.  A couple of convenience timers
   are provided.

   This code rarely needs to change.
   -----------------------------------------------------------------------------
*/

#include "board.h"

#include "stm32f4xx.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_iwdg.h"
#include "stm32f4xx_ll_pwr.h"
#include "stm32f4xx_ll_rcc.h"
#include "stm32f4xx_ll_system.h"
#include "stm32f4xx_ll_tim.h"

#include "board_internal.h"
#include "trace.h"

#ifdef RTT_ENABLED
#include "SEGGER_RTT.h"
#endif


/*
 * Peripheral initialisation calls.  Clock must be done first.
 */
static bool clock_init();
static void fpu_init();
static bool led_init();
static bool delay_tim_init();
static void fault_handlers_init(void);

/*
 * Intialise the board.
 */
bool board_init() {
#ifdef RTT_ENABLED
  SEGGER_RTT_Init();
#endif

  if (!clock_init()) {
    return false;
  }

  if (!led_init()) {
    return false;
  }

  if (!delay_tim_init()) {
    return false;
  }

  fpu_init();
  fault_handlers_init();

  /* Report a restart caused by the watchdog (lights the fault LED) */
  if (LL_RCC_IsActiveFlag_IWDGRST()) {
    ERROR_LOG("Restarted by the watchdog - the superloop stalled");
  }
  LL_RCC_ClearResetFlags();

  /* Art flash cache, should achieve close to 1 wait-state */
  LL_FLASH_EnablePrefetch();
  LL_FLASH_EnableInstCache();
  LL_FLASH_EnableDataCache();

#ifdef SYSVIEW_ENABLED
  SEGGER_SYSVIEW_PrintfTarget("INFO: Sysview Enabled\n");
#endif

#ifdef TRACE_BSP
  RTT_LOG("%sINFO: bsp_init() complete.\n", RTT_CTRL_TEXT_BRIGHT_GREEN);
#endif
  return true;
}

/*
 * 100Mhz clock using external HSE and PLL.
 */
static bool clock_init() {
  LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_PWR);

  /* Set highest voltage scaling for regulator */
  LL_PWR_SetRegulVoltageScaling(LL_PWR_REGU_VOLTAGE_SCALE1);

  /* Set flash latency for higher clock speed */
  LL_FLASH_SetLatency(LL_FLASH_LATENCY_3);
  while (LL_FLASH_GetLatency() != LL_FLASH_LATENCY_3)
    ;

  /* External oscillator */
  LL_RCC_HSE_Enable();
  while (!LL_RCC_HSE_IsReady())
    ;

  /* Configure PLL */
  LL_RCC_PLL_ConfigDomain_SYS(LL_RCC_PLLSOURCE_HSE, LL_RCC_PLLM_DIV_12, 96,
                              LL_RCC_PLLP_DIV_2);
  LL_RCC_PLL_Enable();
  while (!LL_RCC_PLL_IsReady())
    ;

  /* Wait for power to stabilise */
  while (LL_PWR_IsActiveFlag_VOS() == 0)
    ;

  /* Clock source set to the PLL */
  LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_PLL);
  while (LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_PLL)
    ;

  /* Scale the bus clocks - note that APB1 runs at half speed (50MHz)*/
  LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_1);
  LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_2);
  LL_RCC_SetAPB2Prescaler(LL_RCC_APB2_DIV_1);

  SystemCoreClock = 100000000;

  return true;
}

/*
 * LEDs - these light to indicate a fault and an optional heartbeat strobe.
 */
static bool led_init() {
  LL_AHB1_GRP1_EnableClock(LED_FAULT_IO_CLK);

  LL_GPIO_InitTypeDef gpio = {.Pin = LED_FAULT_PIN,
                              .Mode = LL_GPIO_MODE_OUTPUT,
                              .OutputType = LL_GPIO_OUTPUT_PUSHPULL,
                              .Pull = LL_GPIO_PULL_NO};

  if (LL_GPIO_Init(LED_FAULT_PORT, &gpio) != SUCCESS) {
    ERROR_LOG("Unable to initialise fault LED GPIO.");
    return false;
  }

  LED_FAULT_OFF();

#ifdef STROBE_ENABLED
  LL_AHB1_GRP1_EnableClock(LED_STROBE_IO_CLK);

  gpio.Pin = LED_STROBE_PIN;

  if (LL_GPIO_Init(LED_STROBE_PORT, &gpio) != SUCCESS) {
    ERROR_LOG("Unable to initialise strobe LED GPIO.");
    return false;
  }

  led_strobe_off();

#endif

  return true;
}

/*
 * Heartbeat strobe LED control.
 */
void led_strobe_on(void) {
  LL_GPIO_ResetOutputPin(LED_STROBE_PORT, LED_STROBE_PIN);
}

void led_strobe_off(void) {
  LL_GPIO_SetOutputPin(LED_STROBE_PORT, LED_STROBE_PIN);
}

/*
 * FPU: Full FPU support with subnormal rounding.
 */
static void fpu_init() {
  /* Enable */
  SCB->CPACR |= ((3UL << 10 * 2) | (3UL << 11 * 2));

  /* Subnormals as zeroes */
  __set_FPSCR(__get_FPSCR() | (1 << 24));
  __ISB();
  __DSB();
}

/*
 * Enable the configurable fault handlers.  Out of reset they are disabled
 * and every fault escalates to HardFault, so MemManage_Handler(),
 * BusFault_Handler() and UsageFault_Handler() below would never run and the
 * log would only ever say "HardFault".
 */
static void fault_handlers_init(void) {
  SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk |
                SCB_SHCSR_USGFAULTENA_Msk;
  __DSB();
  __ISB();
}

/*
 * Independent watchdog (IWDG), release builds only (WATCHDOG_ENABLED).
 *
 * If the superloop stops running - a hang, a stuck bus wait, an ERROR_HALT -
 * the watchdog resets the MCU rather than leaving the instrument dead, and
 * board_init() reports the restart.  It is left out of debug builds, where a
 * reset would destroy the state at an assert or breakpoint you want to see;
 * it is also frozen while the core is halted by a debugger.
 *
 * Timeout: LSI (~32kHz, 17-47kHz across parts) / 32 = ~1kHz, reload 500 =
 * ~500ms (at least ~340ms).  The longest intended stall is a patch save
 * (~20ms), so there is a wide margin.  Start it after initialisation - the
 * LCD reset sequence alone blocks for ~0.5s.
 */
#define WATCHDOG_RELOAD (500u)

void board_watchdog_start(void) {
#ifdef WATCHDOG_ENABLED
  LL_DBGMCU_APB1_GRP1_FreezePeriph(LL_DBGMCU_APB1_GRP1_IWDG_STOP);

  LL_IWDG_Enable(IWDG); /* also starts the LSI clock */
  LL_IWDG_EnableWriteAccess(IWDG);
  LL_IWDG_SetPrescaler(IWDG, LL_IWDG_PRESCALER_32);
  LL_IWDG_SetReloadCounter(IWDG, WATCHDOG_RELOAD);
  while (!LL_IWDG_IsReady(IWDG))
    ;
  LL_IWDG_ReloadCounter(IWDG);
#endif
}

/*
 * Called once per superloop pass.
 */
void board_watchdog_kick(void) {
#ifdef WATCHDOG_ENABLED
  LL_IWDG_ReloadCounter(IWDG);
#endif
}

/*
 * TIM_DELAY: free-running 1us tick used for  general bus timing.
 */
static bool delay_tim_init() {
  LL_APB2_GRP1_EnableClock(TIM_DELAY_PERIPH_CLK);

  LL_TIM_InitTypeDef tim = {.Prescaler = (100 - 1),
                            .CounterMode = LL_TIM_COUNTERMODE_UP,
                            .Autoreload = 0xFFFF,
                            .ClockDivision = LL_TIM_CLOCKDIVISION_DIV1};

  if (LL_TIM_Init(TIM_DELAY, &tim) != SUCCESS) {
    ERROR_LOG("Unable to initialise delay timer.");
    return false;
  }

  LL_TIM_EnableCounter(TIM_DELAY);

  return true;
}


/*
 * Blocking microsecond (us) delay.
 */
void delay_us(uint16_t us) {
  uint16_t start = LL_TIM_GetCounter(TIM_DELAY);
  while ((uint16_t)(LL_TIM_GetCounter(TIM_DELAY) - start) < us)
    ;
}

/*
 * Blocking millisecond (ms) delay.
 */
void delay_ms(uint16_t ms) {
  while (ms--) {
    delay_us(1000);
  }
}

/*
 * 1us tick.
 */
uint16_t tick_us(void) { return LL_TIM_GetCounter(TIM_DELAY); }


/*
 * Error handler.  Logs the error, illuminates the fault LED (if possible)
 * and optionally halts (loops) the processor to prevent any further processing.
 * In release builds the watchdog then resets the MCU (see
 * board_watchdog_start()).
 *
 * Below this are the standard error handlers for unhandled MCU events.
 */
void log_error_halt(const char *string, bool halt) {
  RTT_LOG("%s%s: %s.\n",
          halt ? RTT_CTRL_TEXT_BRIGHT_RED : RTT_CTRL_TEXT_BRIGHT_YELLOW,
          halt ? "ERROR (HALTED)" : "ERROR (CONTINUING)", string);

  LED_FAULT_ON();

  while (halt)
    ;
}

void NMI_Handler(void) { log_error_halt("MCU Hardware fault (NMI)", true); }

void HardFault_Handler(void) {
  log_error_halt("Processor hardware fault (HardFault)", true);
}

void MemManage_Handler(void) {
  log_error_halt("Invalid memory access (MemProtectFault)", true);
}

void BusFault_Handler(void) {
  log_error_halt("Hardware not present (BusFault)", true);
}

void UsageFault_Handler(void) {
  log_error_halt("Invalid instruction (UsageFault)", true);
}