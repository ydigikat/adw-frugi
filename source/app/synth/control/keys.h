/* -----------------------------------------------------------------------------
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
*/

#ifndef FRUGI_KEYS_H
#define FRUGI_KEYS_H

#include <stdbool.h>
#include <stdint.h>

/* keys_last_down() result when no key is down */
#define KEYS_NONE (0xFFu)

/*
 * The events Keys produces (design-voice-architecture.md §4.3).  The voice
 * assigner registers these at keys_init(); ctx is passed back unchanged so
 * Keys needs no knowledge of what receives them.
 *
 *
 * You may wonder why pass a void *ctx (context) with each event.
 *
 * Firstly, this is a fairly common pattern for eventing since often an event
 * is reused in different contexts, however that is not the case in Frugi.
 *
 * For Frugi, it is always the synth structure, so we could use a struct 
 * synth* with a forward declaration in keys.h (so it compiles cleanly). 
 * This would add type safety and maybe make the intent clearer.
 *
 * I've left it void because keys.c (as the bottom layer) should not depend on
 * layers above. A forward decl in keys.h tightly couples it to the synth 
 * module which architecturally depends on keys rather than the other way.
 * 
 * Keeping it as an opaque pointer also lets the receiver of the event be 
 * anything, allowing for easy unit test mocking, the context used does not
 * need to be a fully initialised synth instance.
 *
 * The downside is we need to be careful to pass the correct type of pointer
 * and must cast back to the correct type when access to the structure members 
 * is required.
 *
 * key_down : a key went down (also sent for a re-press of a held note)
 * key_up   : a key came up - the note may still be held by a pedal
 * gate_off : the note has lost its last reason to sound (key up and no pedal
 *            holding it).  Sent exactly once per note, after any key_up.
 */
struct keys_events {
  void (*key_down)(void *ctx, uint8_t note, uint8_t velocity);
  void (*key_up)(void *ctx, uint8_t note);
  void (*gate_off)(void *ctx, uint8_t note);
};

/*
 * This holds the physical state of key presses and pedals for both poly
 * and monophonic modes.  The note sets are bitsets of 128 bits, one per MIDI
 * note.
 */
struct keys {
  uint8_t velocity[128];  /* last key-down velocity */
  uint32_t down_seq[128]; /* press order, mono last-note priority */
  uint32_t seq;           /* press counter */
  uint8_t down[16];       /* bitset: key is physically down */
  uint8_t sustained[16];  /* bitset: released while the sustain pedal is down */
  uint8_t captured[16];   /* bitset: held by the sostenuto pedal */
  bool sustain_pedal;     /* sustain pedal down */
  bool sostenuto_pedal;   /* sostenuto pedal down */

  const struct keys_events *events; /* where events go (may be NULL) */
  void *ctx;                        /* passed back with every event */
};

/* Set up, with nothing down, and register the event handlers */
void keys_init(struct keys *keys, const struct keys_events *events, void *ctx);

/* Inputs - what the player does */
void keys_key_down(struct keys *keys, uint8_t note, uint8_t velocity);
void keys_key_up(struct keys *keys, uint8_t note);
void keys_sustain(struct keys *keys, bool down);
void keys_sostenuto(struct keys *keys, bool down);

/* Bulk operations (MIDI channel mode messages - see §8 D2) */
void keys_all_keys_up(struct keys *keys);    /* CC123: pedals still apply */
void keys_release_pedals(struct keys *keys); /* CC121: lift both pedals */
void keys_reset(struct keys *keys);          /* forget everything, silently */

/* Queries */
bool keys_is_down(const struct keys *keys, uint8_t note);
bool keys_is_gated(const struct keys *keys, uint8_t note);
bool keys_any_down(const struct keys *keys);
bool keys_any_gated(const struct keys *keys);
uint8_t keys_last_down(const struct keys *keys);
uint8_t keys_velocity(const struct keys *keys, uint8_t note);

#endif /* FRUGI_KEYS_H */
