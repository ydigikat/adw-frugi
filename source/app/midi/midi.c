/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   The MIDI processing routine.  

   This drains the MIDI buffer, calling the parser to convert the data into
   complete MIDI messages and executing a callback to the synthesiser to
   have the messages dispatched.

   The routine will yield if an audio interrupt is received to allow the audio
   engine to execute.

   This does no actual MIDI processing, it is just the glue.
   -----------------------------------------------------------------------------
*/
#include "midi.h"
#include "board.h"
#include "midi_uart.h"
#include "trace.h"

/*
 * Debug warning threshold for the MIDI receive backlog, in bytes - 75% of the
 * 128-byte DMA buffer (MIDI_DMA_BUFFER_SIZE in midi_uart.c).  The circular
 * DMA cannot report a true overrun because unread bytes are silently 
 * overwritten). This is a warning that the superloop is falling behind.
 */
#ifndef MIDI_OVERRUN_LEVEL
#define MIDI_OVERRUN_LEVEL (96u)
#endif

/*
 * MIDI parser instance data.
 */
static struct midi_inst midi_in;

/*
 * The function to call to pass a MIDI message to the audio generator, and
 * its context.  These are passed to us in the midi_init() function call;
 * this module never looks inside the context.
 */
static midi_msg_fn process_midi_msg;
static void *process_midi_msg_ctx;

/*
 * Audio interrupt flag, this routine cooperatively yields processing time if
 * set.
 */
static bool volatile *audio_interrupt;

void midi_routine() {
  uint8_t byte;

#ifdef MIDI_REPORT_OVERRUN
  
  static uint32_t backlog_peak = 0;
  uint32_t backlog = midi_pending_bytes();

  if (backlog > MIDI_OVERRUN_LEVEL && backlog > backlog_peak) {
    backlog_peak = backlog;
    RTT_LOG("%sMIDI OVERRUN WARNING: backlog %u bytes (warning level %u)\n",
            RTT_CTRL_TEXT_BRIGHT_RED, (unsigned)backlog,
            (unsigned)MIDI_OVERRUN_LEVEL);
  }
#endif

  while (!(*audio_interrupt) && midi_poll_byte(&byte)) {
    struct midi_msg *msg = midi_parse(&midi_in, byte, tick_us());
    if (msg) {
      process_midi_msg(process_midi_msg_ctx, msg);
    }
  }
}

bool midi_init(midi_msg_fn process_midi_msg_cb, void *ctx, uint8_t channel,
               volatile bool *audio_interrupt_flag) {
  RTT_ASSERT(process_midi_msg_cb);

  midi_in.channel = channel;
  process_midi_msg = process_midi_msg_cb;
  process_midi_msg_ctx = ctx;
  audio_interrupt = audio_interrupt_flag;

  if (!midi_start()) {
    return false;
  }

  return true;
}
