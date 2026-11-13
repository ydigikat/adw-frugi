/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   This module contains diagnostic and DWT timing macros.  These output their
   results via Segger RTT and so require the use of a J-Link probe.

   STM ST-Link probes could modify this code to work with SWO.  Clone ST-Link
   probes do not connect the SWO wire so cannot be used at all (without a bodge).

   If RTT_ENABLED is not set then these compile out to nothing - except in a
   host build with TRACE_TO_STDOUT defined, where RTT_LOG goes to printf so
   the per-module traces (TRACE_KEYS, TRACE_ASSIGN, ...) can be run and read
   on a PC, e.g. from a unit test.
   -----------------------------------------------------------------------------
*/
#ifndef FRUGI_TRACE_H
#define FRUGI_TRACE_H

/* Disable RTT for unit tests - host not embedded compilation */
#ifdef UNIT_TESTS
#undef RTT_ENABLED
#endif

#include <math.h>

#ifdef RTT_ENABLED
#include "SEGGER_RTT.h"
#include "stm32f411xe.h"
#define RTT_TERMINAL (0)

#ifdef DEBUG

/*
 * Assertion macro : Only active when both RTT and DEBUG are enabled
 *
 * Example: RTT_ASSERT(buffer != NULL);
 *
 * If the check fails, it prints the file, function and line number where it
 * happened to RTT then hangs the system (infinite loop) since continuing would
 * be dangerous
 */
#define RTT_ASSERT(expr)                                                       \
  do {                                                                         \
    if (!(expr)) {                                                             \
      SEGGER_RTT_printf(0, "ASSERTION FAILED: %s, function: %s, line: %d\n",   \
                        __FILE__, __FUNCTION__, __LINE__);                     \
      while (1)                                                                \
        ;                                                                      \
    }                                                                          \
  } while (0)
#else

/* set to nothing in Release modes */
#define RTT_ASSERT(expr) ((void)0)

#endif /* DEBUG */

/* Following macros need RTT and work for both DEBUG/RELEASE presets */

/*
 * Fast, bounded float trace formatter.
 * Range: approximately -2147483.647f to +2147483.647f.
 */
static inline void RTT_LogFloat3(const char *prefix, float value) {
  const uint32_t scale = 1000u;
  uint32_t scaled;
  uint32_t whole;
  uint32_t fraction;
  const char *sign = "";

  if (isnan(value)) {
    SEGGER_RTT_printf(RTT_TERMINAL, "%snan", prefix);
    return;
  }

  /*
   * Keep the scaled value inside signed 32-bit range.
   */
  if (value > 2147483.0f) {
    SEGGER_RTT_printf(0, "%s+overflow", prefix);
    return;
  }

  if (value < -2147483.0f) {
    SEGGER_RTT_printf(0, "%s-overflow", prefix);
    return;
  }

  if (value < 0.0f) {
    sign = "-";
    value = -value;
  }

  /*
   * Single-precision multiply and round-to-nearest at 0.001 resolution.
   */
  scaled = (uint32_t)(value * 1000.0f + 0.5f);
  whole = scaled / scale;
  fraction = scaled % scale;

  SEGGER_RTT_printf(RTT_TERMINAL, "%s%s%lu.%03lu", prefix, sign,
                    (unsigned long)whole, (unsigned long)fraction);
}

/*
 * Log
 */
#define RTT_LOG_FLOAT(label, value)                                            \
  do {                                                                         \
    RTT_LogFloat3((label), (float)(value));                                    \
    SEGGER_RTT_WriteString(RTT_TERMINAL, "\n");                                \
  } while (0)

/*
 * The main debug print function when you don't need floating point.
 */
#define RTT_LOG(fmt, ...) SEGGER_RTT_printf(RTT_TERMINAL, fmt, ##__VA_ARGS__);

/*
 * Colour the following RTT output (terminal colour escape codes).  Used to
 * make warnings stand out, e.g. RTT_RED(); RTT_LOG(...); RTT_WHITE();
 */
#define RTT_RED() SEGGER_RTT_WriteString(RTT_TERMINAL, RTT_CTRL_TEXT_BRIGHT_RED)
#define RTT_BLUE()                                                             \
  SEGGER_RTT_WriteString(RTT_TERMINAL, RTT_CTRL_TEXT_BRIGHT_BLUE)
#define RTT_WHITE()                                                            \
  SEGGER_RTT_WriteString(RTT_TERMINAL, RTT_CTRL_TEXT_BRIGHT_WHITE)

/*
 * Data Watchpoint Timer counter setup - used for timing measurements
 * This initializes the ARM Cortex debug hardware for cycle counting and only
 * needs calling once.
 */
#define DWT_INIT()                                                             \
  uint32_t dwt_start, dwt_end, dwt_cycles;                                     \
  uint32_t dwt_time_us;                                                        \
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;                              \
  DWT->CYCCNT = 0;                                                             \
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

/*
 * Resets and starts the cycle counter
 * Call this right before the code you want to measure
 */
#define DWT_CLEAR()                                                            \
  DWT->CYCCNT = 0;                                                             \
  dwt_start = DWT->CYCCNT;

/*
 * CPU cycles per microsecond - the 100MHz core clock set in board.c.
 */
#define DWT_CYCLES_PER_US (100u)

/*
 * Outputs the timing measurement to RTT
 * Call this right after the code you want to measure
 * Shows both CPU cycles and approximate time in microseconds
 */
#define DWT_OUTPUT(msg)                                                        \
  dwt_end = DWT->CYCCNT;                                                       \
  dwt_cycles = dwt_end - dwt_start;                                            \
  dwt_time_us = dwt_cycles / DWT_CYCLES_PER_US;                          \
  RTT_LOG("%s # %s : %lu cycles (~%lu us)\n", RTT_CTRL_TEXT_BRIGHT_CYAN, msg,  \
          dwt_cycles, dwt_time_us);

#elif defined(TRACE_TO_STDOUT)
/*
 * Host build: traces print to stdout, without the terminal colours.
 */
#include <stdio.h>

#define RTT_CTRL_TEXT_WHITE ""
#define RTT_CTRL_TEXT_BRIGHT_WHITE ""
#define RTT_CTRL_TEXT_BRIGHT_RED ""
#define RTT_CTRL_TEXT_BRIGHT_GREEN ""
#define RTT_CTRL_TEXT_BRIGHT_BLUE ""
#define RTT_CTRL_TEXT_BRIGHT_CYAN ""
#define RTT_CTRL_TEXT_BRIGHT_YELLOW ""
#define RTT_CTRL_TEXT_BRIGHT_MAGENTA ""

#define RTT_LOG(...) printf(__VA_ARGS__)
#define RTT_LOG_FLOAT(label, value) printf("%s%.3f\n", (label), (double)(value))
#define RTT_RED()
#define RTT_BLUE()
#define RTT_WHITE()
#define RTT_ASSERT(expr)
#define DWT_INIT()                                                             \
  uint32_t dwt_time_us = 0;                                                    \
  (void)dwt_time_us;
#define DWT_CLEAR()
#define DWT_OUTPUT(function)

#else
/*
 * When RTT is disabled, all macros become empty
 */

#define RTT_LOG(...)
#define RTT_LOG_FLOAT(label, value)
#define RTT_RED()
#define RTT_BLUE()
#define RTT_WHITE()
#define RTT_ASSERT(expr)
/* Declares the result variable so code that reads it still compiles */
#define DWT_INIT()                                                             \
  uint32_t dwt_time_us = 0;                                                    \
  (void)dwt_time_us;
#define DWT_CLEAR()
#define DWT_OUTPUT(function)

#endif /* RTT_ENABLED */

#endif /* FRUGI_TRACE_H */