/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   Frugi: A vintage style, subtractive, 8-voice polyphonic synthesiser for 
   the STM32F411CEU6 MCU.

   Built with HEADLESS defined (CMake option HEADLESS), Frugi is the original
   MIDI-only design: no UI and no patch storage, just the audio and MIDI
   routines driving the synth.  Every parameter has a MIDI CC, so it can
   still be fully programmed; Program Change loads the default patch.
   -----------------------------------------------------------------------------
*/

#include <stdbool.h>

#include "audio.h"
#include "board.h"
#include "midi.h"
#include "synth.h"

#ifndef HEADLESS
#include "patch_persist.h"
#include "ui.h"
#endif

/* The synthesiser data instance and 'glue' functions*/
static struct synth s;
static void process_midi_msg(void *ctx, struct midi_msg *msg);
static void process_block(void *ctx, float *left, float *right);

static void init();

#ifdef STROBE_ENABLED
static void heartbeat_routine(void);
#endif

/*
 * Audio interrupt flag, this is passed to all tasks, the audio routine
 * will handle it and other routines will cooperatively yield to it.
 */
static volatile bool audio_interrupt_flag = false;



int main(void) {
  
  /* Initialise the system, this will log and halt the MCU if it fails */
  init();

  /* Started after init: initialisation blocks for longer than the timeout */
  board_watchdog_start();

  /*
   * Superloop(foreground/background system).
   *
   * For a single-purpose audio instrument, an interrupt driven loop, if
   * correctly cooperative in nature, is architecturally cleaner with
   * a lower overhead than using an RTOS.
   */
  while (1) {
    audio_routine();
    midi_routine();
#ifndef HEADLESS
    ui_routine();
#endif
#ifdef STROBE_ENABLED
    heartbeat_routine();
#endif
    board_watchdog_kick();
  }

  /* NOTREACHED */
}

/*
* Initialises the system and shares the audio_interrupt flag across the various
* routines.  
*/
static void init()
{
  if (!board_init()) {
    ERROR_HALT("Unable to initialise board.");
  }

#ifdef HEADLESS
  /* No patch store: Program Change falls back to the default patch */
  synth_init(&s, MIDI_OMNI, NULL);
#else
  if (!patch_persist_init()) {
    ERROR_HALT("Unable to initialise patch EEPROM.");
  }

  synth_init(&s, MIDI_OMNI, patch_persist_load);
#endif

  if (!audio_init(process_block, &s, &audio_interrupt_flag)) {
    ERROR_HALT("Unable to initialise audio.");
  }

  if (!midi_init(process_midi_msg, &s, s.midi_channel,
                 &audio_interrupt_flag)) {
    ERROR_HALT("Unable to initialise MIDI.");
  }

#ifndef HEADLESS
  if (!ui_init(&s, &audio_interrupt_flag)) {
    ERROR_HALT("Unable to initialise UI.");
  }
#endif
}

/*
 * Wires the audio routine to the synthesiser.  ctx is the synth registered
 * in init() - the audio module only passes it back.
 */
static void process_block(void *ctx, float *left, float *right) {
  struct synth *synth = ctx;
  synth_render(synth, left, right);
}

/*
 * Wires the MIDI routine to the synthesiser, as above.
 */
static void process_midi_msg(void *ctx, struct midi_msg *msg) {
  struct synth *synth = ctx;
  synth_handle_midi(synth, msg->data[0], msg->data[1], msg->data[2]);
}

#ifdef STROBE_ENABLED
/* Heartbeat LED flash timing */
#define STROBE_ON_MS (50)
#define STROBE_OFF_MS (950)

/*
 * Heartbeat indicator - a short flash once a second shows the superloop is
 * running (in both the headless and full builds).
 */
static void heartbeat_routine(void) {
  static bool strobe_on = false;
  static uint32_t elapsed_us = 0;
  static uint16_t last_tick_us = 0;

  uint16_t now = tick_us();
  elapsed_us += (uint16_t)(now - last_tick_us);
  last_tick_us = now;

  if (strobe_on) {
    if (elapsed_us >= (uint32_t)STROBE_ON_MS * 1000) {
      led_strobe_off();
      strobe_on = false;
      elapsed_us = 0;
    }
  } else {
    if (elapsed_us >= (uint32_t)STROBE_OFF_MS * 1000) {
      led_strobe_on();
      strobe_on = true;
      elapsed_us = 0;
    }
  }
}
#endif
