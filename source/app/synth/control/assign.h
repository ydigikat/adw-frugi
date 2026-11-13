/* -----------------------------------------------------------------------------
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
*/
#ifndef FRUGI_ASSIGN_H
#define FRUGI_ASSIGN_H

#include <stdint.h>

struct synth;

/*
 * Voice assigner - turns key events from the keys layer into voice commands
 * (design-voice-architecture.md section 5).
 *
 * Voice mode is a choice of strategy: synth->assigner points at one of the
 * two below.  This is the same jump-table idiom the DSP modules and the voice
 * state machine use, and it is the only place voice mode is tested.
 *
 * key_down  : a key went down (from the keys layer)
 * key_up    : a key came up - the note may still be held by a pedal
 * gate_off  : the note has lost its last reason to sound
 * all_off   : silence every voice now (mode change, panic)
 * update    : performance parameters changed (e.g. unison detune) - push
 *             the strategy's per-voice settings to the voices
 * mix_scale : gain applied to the summed voices
 */
struct assigner {
  const char *name;
  void (*key_down)(struct synth *synth, uint8_t note, uint8_t velocity);
  void (*key_up)(struct synth *synth, uint8_t note);
  void (*gate_off)(struct synth *synth, uint8_t note);
  void (*all_off)(struct synth *synth);
  void (*update)(struct synth *synth);
  float (*mix_scale)(const struct synth *synth);
};

/*
 * Mix gain for one voice playing its own note - conservative, to allow for
 * DCO/EG overshoot and DCF resonance across up to 8 voices.  The mono
 * assigner scales its gain from this so unison is as loud as a poly voice.
 */
#define ASSIGN_VOICE_GAIN (0.4564f) /* 1 / sqrt(0.6 * 8) */

extern const struct assigner poly_assigner;
extern const struct assigner mono_assigner;

#endif /* FRUGI_ASSIGN_H */
