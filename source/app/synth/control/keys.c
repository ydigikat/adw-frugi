/* -----------------------------------------------------------------------------
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   This module is analogous to the keyboard scanner (usually an MCU) found in
   1980s polysynths.

   Its job is to understand which keys are down, including any held by
   sustain or sostenuto pedals.  It tracks what the player is doing and works
   out, in one central module, when each note's gate should fall.

   It has no knowledge of voices, stealing or envelopes, it is only concerned
   with implementing the logic around note on/off state.

   A note is gated (on) while its key is down, or it was released but the
   sustain pedal was down (and not lifted), or it was captured by the
   sostenuto pedal (and that has not been lifted).

   Every input updates the state and then fires what changed as events.

   -----------------------------------------------------------------------------
   TRACE_KEYS: logs each input with the resulting state, and every gate_off
               with the reason it went (OFF).  The state is shown as
               [D<keys down> S<sustained> C<captured> sus+/- sos+/-].
   -----------------------------------------------------------------------------
*/
#include <string.h>

#include "keys.h"


#define NOTE_COUNT (128u)
#define SET_BYTES (NOTE_COUNT / 8u)

/* -------------------------------------------------------------------------
 * 128-bit note sets, these represent the entire playable keyboard range.
 * ---------------------------------------------------------------------- */

/* Is a note in the set? */
static inline bool set_has(const uint8_t set[SET_BYTES], uint8_t note) {
  return (set[note >> 3] & (uint8_t)(1u << (note & 7u))) != 0;
}

/* Add a note to the set (on) */
static inline void set_add(uint8_t set[SET_BYTES], uint8_t note) {
  set[note >> 3] |= (uint8_t)(1u << (note & 7u));
}

/* Remove a note from the set (off) */
static inline void set_remove(uint8_t set[SET_BYTES], uint8_t note) {
  set[note >> 3] &= (uint8_t)~(1u << (note & 7u));
}

/* Are any note in the set? */
static bool set_any(const uint8_t set[SET_BYTES]) {
  for (uint32_t i = 0; i < SET_BYTES; i++) {
    if (set[i]) {
      return true;
    }
  }
  return false;
}

/* -------------------------------------------------------------------------
 * Tracing
 * ---------------------------------------------------------------------- */

#ifdef TRACE_KEYS
#include "trace.h"

static uint32_t set_count(const uint8_t set[SET_BYTES]) {
  uint32_t count = 0;
  for (uint32_t i = 0; i < SET_BYTES; i++) {
    count += (uint32_t)__builtin_popcount(set[i]);
  }
  return count;
}

static void trace_state(const struct keys *keys) {
  RTT_LOG(" [D%u S%u C%u sus%c sos%c]\n", (unsigned)set_count(keys->down),
          (unsigned)set_count(keys->sustained),
          (unsigned)set_count(keys->captured),
          keys->sustain_pedal ? '+' : '-', keys->sostenuto_pedal ? '+' : '-');
}

#define KEYS_LOG(...) RTT_LOG(__VA_ARGS__)
#define KEYS_STATE(keys) trace_state(keys)
#else
#define KEYS_LOG(...)
#define KEYS_STATE(keys)
#endif

/* -------------------------------------------------------------------------
 * Events, these are fired when something 'changes'
 * ---------------------------------------------------------------------- */

static void emit_key_down(const struct keys *keys, uint8_t note,
                          uint8_t velocity) {
  if (keys->events && keys->events->key_down) {
    keys->events->key_down(keys->ctx, note, velocity);
  }
}

static void emit_key_up(const struct keys *keys, uint8_t note) {
  if (keys->events && keys->events->key_up) {
    keys->events->key_up(keys->ctx, note);
  }
}

static void emit_gate_off(const struct keys *keys, uint8_t note,
                          const char *why) {
  (void)why; /* only used by the trace */
  KEYS_LOG("KEYS: gate_off %u (%s)\n", note, why);

  if (keys->events && keys->events->gate_off) {
    keys->events->gate_off(keys->ctx, note);
  }
}

/*
 * Remove a pedal's hold from every note in `held`, sending gate_off for each
 * note that no longer has any reason to sound.  Notes are visited in
 * ascending order, so the event order is predictable.
 */
static void release_hold(struct keys *keys, uint8_t held[SET_BYTES],
                         const char *why) {
  for (uint32_t n = 0; n < NOTE_COUNT; n++) {
    const uint8_t note = (uint8_t)n;

    if (!set_has(held, note)) {
      continue;
    }

    set_remove(held, note);

    if (!keys_is_gated(keys, note)) {
      emit_gate_off(keys, note, why);
    }
  }
}

/* -------------------------------------------------------------------------
 * Set up
 * ---------------------------------------------------------------------- */

void keys_init(struct keys *keys, const struct keys_events *events, void *ctx) {
  keys->events = events;
  keys->ctx = ctx;
  keys_reset(keys);
}

/*
 * Reset: for power-up and for a hard "all sound off" where the voices are 
 * silenced directly.
 */
void keys_reset(struct keys *keys) {
  memset(keys->velocity, 0, sizeof(keys->velocity));
  memset(keys->down_seq, 0, sizeof(keys->down_seq));
  memset(keys->down, 0, sizeof(keys->down));
  memset(keys->sustained, 0, sizeof(keys->sustained));
  memset(keys->captured, 0, sizeof(keys->captured));
  keys->seq = 0;
  keys->sustain_pedal = false;
  keys->sostenuto_pedal = false;

  KEYS_LOG("KEYS: reset\n");
}

/* -------------------------------------------------------------------------
 * Inputs
 * ---------------------------------------------------------------------- */

/*
 * Key down.  (A key already held may be pressed again - a retrigger)
 *
 * A retrigger clears any sustain hold (the key itself now takes over the
 * sustain) but retains any sostenuto capture: the note was sounding when 
 * that pedal went down and the pedal is still down.
 *
 * Callers translate a Note On with velocity 0 into keys_key_up().
 */
void keys_key_down(struct keys *keys, uint8_t note, uint8_t velocity) {
  if (note >= NOTE_COUNT) {
    return;
  }

  set_add(keys->down, note);
  set_remove(keys->sustained, note);
  keys->velocity[note] = velocity;
  keys->down_seq[note] = ++keys->seq;

  KEYS_LOG("KEYS: down %u v%u", note, velocity);
  KEYS_STATE(keys);

  emit_key_down(keys, note, velocity);
}

/*
 * Key up.  With the sustain pedal down the note remains sustained.
 *
 * - a gate_off only follows the key_up if nothing else is holding the note.  
 * - a key_up for a key that wasn't down is a valid state and ignored.
 */
void keys_key_up(struct keys *keys, uint8_t note) {
  if (note >= NOTE_COUNT || !set_has(keys->down, note)) {
    return;
  }

  set_remove(keys->down, note);

  if (keys->sustain_pedal) {
    set_add(keys->sustained, note);
  }

  KEYS_LOG("KEYS: up %u", note);
  KEYS_STATE(keys);

  emit_key_up(keys, note);

  if (!keys_is_gated(keys, note)) {
    emit_gate_off(keys, note, "key up");
  }
}

/*
 * Sustain pedal down (CC64). Lifting the pedal releases all notes it was holding.
 */
void keys_sustain(struct keys *keys, bool down) {
  if (down == keys->sustain_pedal) {
    return;
  }

  keys->sustain_pedal = down;

  KEYS_LOG("KEYS: sustain %s", down ? "down" : "up");
  KEYS_STATE(keys);

  if (!down) {
    release_hold(keys, keys->sustained, "sustain lifted");
  }
}

/*
 * Sostenuto pedal (CC66).  Pressing it captures every note at that
 * moment - both keys down and notes held by the sustain pedal but nothing 
 * played afterwards.  
 */
void keys_sostenuto(struct keys *keys, bool down) {
  if (down == keys->sostenuto_pedal) {
    return;
  }

  keys->sostenuto_pedal = down;

  if (down) {
    for (uint32_t i = 0; i < SET_BYTES; i++) {
      keys->captured[i] = keys->down[i] | keys->sustained[i];
    }
  }

  KEYS_LOG("KEYS: sostenuto %s", down ? "down" : "up");
  KEYS_STATE(keys);

  if (!down) {
    release_hold(keys, keys->captured, "sostenuto lifted");
  }
}

/* -------------------------------------------------------------------------
 * MIDI CC triggered bulk operations
 * ---------------------------------------------------------------------- */

/*
 * All Notes Off (CC123): every held key comes up, exactly as if released by
 * hand, so the pedals still apply.
 */
void keys_all_keys_up(struct keys *keys) {
  KEYS_LOG("KEYS: all keys up\n");
  for (uint32_t n = 0; n < NOTE_COUNT; n++) {
    keys_key_up(keys, (uint8_t)n);
  }
}

/*
 * Reset All Controllers (CC121) as far as Keys is concerned: both pedals are
 * lifted, releasing whatever they held.  Keys still down keep sounding.
 */
void keys_release_pedals(struct keys *keys) {
  KEYS_LOG("KEYS: release pedals\n");
  keys_sustain(keys, false);
  keys_sostenuto(keys, false);
}

/* -------------------------------------------------------------------------
 * Queries
 * ---------------------------------------------------------------------- */

bool keys_is_down(const struct keys *keys, uint8_t note) {
  return note < NOTE_COUNT && set_has(keys->down, note);
}

/* The single rule - see the top of this file */
bool keys_is_gated(const struct keys *keys, uint8_t note) {
  if (note >= NOTE_COUNT) {
    return false;
  }

  return set_has(keys->down, note) || set_has(keys->sustained, note) ||
         (keys->sostenuto_pedal && set_has(keys->captured, note));
}

bool keys_any_down(const struct keys *keys) { return set_any(keys->down); }

bool keys_any_gated(const struct keys *keys) {
  for (uint32_t n = 0; n < NOTE_COUNT; n++) {
    if (keys_is_gated(keys, (uint8_t)n)) {
      return true;
    }
  }
  return false;
}

/*
 * The most recently pressed key still down (mono last-note priority), or
 * KEYS_NONE.  There is no limit on how many keys can be held.
 */
uint8_t keys_last_down(const struct keys *keys) {
  uint8_t last = KEYS_NONE;
  uint32_t last_seq = 0;

  for (uint32_t n = 0; n < NOTE_COUNT; n++) {
    const uint8_t note = (uint8_t)n;

    if (set_has(keys->down, note) && keys->down_seq[note] > last_seq) {
      last_seq = keys->down_seq[note];
      last = note;
    }
  }

  return last;
}

/* Velocity of the key's most recent press */
uint8_t keys_velocity(const struct keys *keys, uint8_t note) {
  return note < NOTE_COUNT ? keys->velocity[note] : 0;
}
