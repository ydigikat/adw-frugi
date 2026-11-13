/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
*/

#ifndef FRUGI_MIDI_H
#define FRUGI_MIDI_H

#include <stdbool.h>
#include <stdint.h>

#include "midi_parser.h"

/*
 * Message handler, called for each complete MIDI message.
 */
typedef void (*midi_msg_fn)(void *ctx, struct midi_msg *msg);

bool midi_init(midi_msg_fn process_midi_msg_cb, void *ctx, uint8_t channel,
               volatile bool *audio_interrupt_flag);
void midi_routine();

#endif /* FRUGI_MIDI_H */
