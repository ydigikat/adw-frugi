/* -----------------------------------------------------------------------------
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   Monophonic (unison) voice assigner - every voice plays the same note.

   key_down : if no other key is down this is a new phrase and every voice is
              triggered; otherwise it is legato and every voice just changes
              pitch (no retrigger).
   key_up   : if the released key was sounding and other keys are still down,
              go back (legato) to the most recently pressed of them - "return
              to held note", for trills.
   gate_off : release the voices only when nothing at all is held - no key
              down and no pedal holding a note.

   Mono priority uses keys physically down only; notes held just by a pedal
   don't take part (design decision D1).  The keys layer tracks every key, so
   there is no limit on how many can be held (D7).

   Unison: each voice gets its own detune offset and oscillator start phase
   from fixed, deliberately irregular tables (update()), and the mix gain
   follows whether the voices add in phase or by power (mix_scale()).  See
   design-voice-architecture.md section 5.3.
   -----------------------------------------------------------------------------
   TRACE_ASSIGN: logs each decision (OFF)
   -----------------------------------------------------------------------------
*/
#include <math.h>

#include "assign.h"
#include "keys.h"
#include "synth.h"

#ifdef TRACE_ASSIGN
#include "trace.h"
#define ASSIGN_LOG(...) RTT_LOG(__VA_ARGS__)
#else
#define ASSIGN_LOG(...)
#endif

/*
 * Mix gain, unison should be about as loud as a single poly voice.
 *
 * SYNTH_MAX_VOICES voices playing unison will be in phase, this will add 
 * amplitude, so the gain is ASSIGN_VOICE_GAIN / SYNTH_MAX_VOICES.  
 
 * When detuned, or started at different phases, they add by power 
 * so the gain is ASSIGN_VOICE_GAIN / sqrt(SYNTH_MAX_VOICES).
 */
#define MONO_GAIN_IN_PHASE (ASSIGN_VOICE_GAIN / SYNTH_MAX_VOICES)

/*
 * Unison voice fixed offset/phase data tables.  
 *
 * Offsets: each voice's part of the sonic landscape.  
 * Phases: each voice's oscillator start phase between 0 and 1.0
 */
#if SYNTH_MAX_VOICES == 8
static const float unison_offset[8] = {-0.500f, -0.384f, -0.266f, 0.040f,
                                       0.168f,  0.239f,  0.311f,  0.392f};
static const float unison_phase[8] = {0.666f, 0.391f, 0.594f, 0.381f,
                                      0.981f, 0.455f, 0.683f, 0.235f};
#elif SYNTH_MAX_VOICES == 6
static const float unison_offset[6] = {-0.352f, -0.246f, -0.102f,
                                       -0.039f, 0.239f,  0.500f};
static const float unison_phase[6] = {0.934f, 0.578f, 0.518f,
                                      0.804f, 0.504f, 0.113f};
#else
#error "unison tables only cover SYNTH_MAX_VOICES == 6 or 8"
#endif

/* Is any key other than `note` down? */
static bool other_key_down(const struct keys *keys, uint8_t note) {
  for (uint32_t n = 0; n < 128u; n++) {
    if (n != note && keys_is_down(keys, (uint8_t)n)) {
      return true;
    }
  }
  return false;
}

static void mono_key_down(struct synth *synth, uint8_t note, uint8_t velocity) {
  if (!other_key_down(&synth->keys, note)) {
    /*
     * New mono phrase.  A voice still releasing the last phrase is stolen, and 
     * one still in steal (RTZ) is retargeted.
     */
    ASSIGN_LOG("ASSIGN mono: %u new phrase\n", note);

    const struct voice *v0 = &synth->voice[0]; 
    const bool retrigger = (v0->state == VOICE_ACTIVE && v0->note == note);

    if (!retrigger) {
      synth->unison_in_phase = (synth->unison_detune <= 0.0f);
    }

    for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
      voice_trigger(&synth->voice[i], note, velocity);
    }
  } else {
    ASSIGN_LOG("ASSIGN mono: %u legato\n", note);
    for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
      voice_set_note(&synth->voice[i], note);
    }
  }

  synth->mono_note = note;
}

static void mono_key_up(struct synth *synth, uint8_t note) {
  if (note != synth->mono_note) {
    return; /* an older held key - the sounding note is unchanged */
  }

  const uint8_t held = keys_last_down(&synth->keys);

  if (held == KEYS_NONE) {
    return; /* last key up - gate_off decides whether to release */
  }

  ASSIGN_LOG("ASSIGN mono: %u up, back to %u\n", note, held);
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    voice_set_note(&synth->voice[i], held);
  }

  synth->mono_note = held;
}

static void mono_gate_off(struct synth *synth, uint8_t note) {
  (void)note;

  if (keys_any_gated(&synth->keys)) {
    return; /* a key or a pedal still holds something */
  }

  ASSIGN_LOG("ASSIGN mono: release\n");
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    voice_gate_off(&synth->voice[i]);
  }

  synth->mono_note = KEYS_NONE;
}

static void mono_all_off(struct synth *synth) {
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    voice_kill(&synth->voice[i]);
  }

  synth->mono_note = KEYS_NONE;
}

/*
 * Unison spread.  Each voice is detuned by its share of the spread and starts 
 * at its own phase.  
 *
 * With no unison the voices all start at the default phase and play exactly
 * the same waveform.
 */
static void mono_update(struct synth *synth) {
  const float spread = synth->unison_detune;

  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    struct voice *v = &synth->voice[i];

    v->unison_cents = unison_offset[i] * spread;
    v->start_phase = (spread > 0.0f) ? unison_phase[i] : -1.0f;

    voice_update(v);
  }

  /*
   * Once detuned the voices will drift apart, and stay apart even if the
   * detune is turned back to zero until the note is released.
   */
  if (spread > 0.0f) {
    synth->unison_in_phase = false;
  }
}

/*
 * Scale a mono voice for mixing
 */
static float mono_mix_scale(const struct synth *synth) {
  return synth->unison_in_phase ? MONO_GAIN_IN_PHASE
                                : ASSIGN_VOICE_GAIN / sqrtf((float)SYNTH_MAX_VOICES);
}

/*
* Assign the event handlers, scaler etc for polyphonic voice mode.
*/
const struct assigner mono_assigner = {.name = "mono",
                                       .key_down = mono_key_down,
                                       .key_up = mono_key_up,
                                       .gate_off = mono_gate_off,
                                       .all_off = mono_all_off,
                                       .update = mono_update,
                                       .mix_scale = mono_mix_scale};
