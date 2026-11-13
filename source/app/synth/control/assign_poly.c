/* -----------------------------------------------------------------------------
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   Polyphonic voice assigner - one voice per note.

   Like the voice assigner of a 1980s polysynth, this decides which voice
   plays each key.  It works only from the keys layer's events and queries and
   the voices' own state; it holds no pedal logic (the keys layer owns that).

   key_down : a voice already sounding the note is retriggered; otherwise an
              idle voice is used; otherwise one is stolen (see find_steal()).
   key_up   : ignored - poly only cares when the gate falls.
   gate_off : the voice sounding the note gets voice_gate_off().

   Nested stealing is not supported: a voice that is mid-steal is never
   chosen again until its steal completes.  

   "Oldest" uses a trigger sequence number per voice (synth->trigger_seq),
   stamped when a voice is given a new note.  It is a uint32_t, so unlike the
   old int8_t age counter it cannot wrap in practice.
   -----------------------------------------------------------------------------
   TRACE_ASSIGN: logs each allocation decision (OFF)
   -----------------------------------------------------------------------------
*/
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
 * The voice speaking, either active or mid-steal and about to become active.  
 * A mid-steal voice is matched on the note it is stealing. The note fading
 * has already been given up.
 */
static struct voice *find_sounding(struct synth *synth, uint8_t note) {
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    struct voice *v = &synth->voice[i];

    if ((v->state == VOICE_ACTIVE && v->note == note) ||
        (v->state == VOICE_STEALING && v->target_note == note)) {
      return v;
    }
  }

  return nullptr;
}

/*
* Finds an idle voice.
*/
static struct voice *find_idle(struct synth *synth) {
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    if (synth->voice[i].state == VOICE_IDLE) {
      return &synth->voice[i];
    }
  }

  return nullptr;
}

/*
 * The best voice to steal:
 *
 * 1. A releasing voice (gate low): it is fading anyway.  The quietest is
 *    chosen, as less of it is cut short.
 * 2. Otherwise the oldest voice whose note is held only by a pedal - its key
 *    is up, the pedal is just extending it.
 * 3. Otherwise the oldest voice whose key is still down.
 *
 * Voices already in mid-steal are ignored (no nested steals in Frugi).
 */
static struct voice *find_steal(struct synth *synth) {
  struct voice *releasing = nullptr;
  struct voice *pedal_only = nullptr;
  struct voice *key_down = nullptr;

  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    struct voice *v = &synth->voice[i];
    const uint32_t seq = synth->trigger_seq[i];

    if (v->state != VOICE_ACTIVE) {
      continue;
    }

    if (!v->gate) {
      if (!releasing || v->amp_env.level < releasing->amp_env.level) {
        releasing = v;
      }
    } else if (!keys_is_down(&synth->keys, v->note)) {
      if (!pedal_only || seq < synth->trigger_seq[pedal_only->idx]) {
        pedal_only = v;
      }
    } else if (!key_down || seq < synth->trigger_seq[key_down->idx]) {
      key_down = v;
    }
  }

  if (releasing) {
    return releasing;
  }

  return pedal_only ? pedal_only : key_down;
}

/* Mark a voice as given a new note - youngest */
static void stamp(struct synth *synth, const struct voice *v) {
  synth->trigger_seq[v->idx] = ++synth->trigger_counter;
}

static void poly_key_down(struct synth *synth, uint8_t note,
                          uint8_t velocity) {
  struct voice *v = find_sounding(synth, note);

  if (v) {
    /* Retrigger.  Not a new allocation */
    ASSIGN_LOG("ASSIGN poly: %u retrigger v%u\n", note, v->idx);
    voice_trigger(v, note, velocity);
    return;
  }

  v = find_idle(synth);
  if (v) {
    ASSIGN_LOG("ASSIGN poly: %u idle v%u\n", note, v->idx);
  } else {
    v = find_steal(synth);
    if (!v) {
      /* Every voice is mid-steal - nothing available */
      ASSIGN_LOG("ASSIGN poly: %u dropped (no voice)\n", note);
      return;
    }
    ASSIGN_LOG("ASSIGN poly: %u steal v%u (note %u)\n", note, v->idx, v->note);
  }

  stamp(synth, v);
  voice_trigger(v, note, velocity);
}

/*
* Event handler: Polyphonic key up (unused) this might still be held
* by a pedal.
*/
static void poly_key_up(struct synth *synth, uint8_t note) {
  (void)synth;
  (void)note;
}

/*
* Event handler: Polyphonic note off, the gate is off, this note is
* now no longer sounding. 
*/
static void poly_gate_off(struct synth *synth, uint8_t note) {
  struct voice *v = find_sounding(synth, note);

  if (v) {
    ASSIGN_LOG("ASSIGN poly: %u gate off v%u\n", note, v->idx);
    voice_gate_off(v);
  }
  /* else: the voice was already stolen for another note so nothing to release */
}

static void poly_all_off(struct synth *synth) {
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    voice_kill(&synth->voice[i]);
  }
}

/*
* We've switched to polyphonic mode and some mono related voice params must be
* turned off otherwise the poly voices will behave incorrectly.
*/
static void poly_update(struct synth *synth) {
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    synth->voice[i].unison_cents = 0.0f;
    synth->voice[i].start_phase = -1.0f;
    voice_update(&synth->voice[i]);
  }
}

/*
* Voice mix scaler to be used in polyphonic mode.
*/
static float poly_mix_scale(const struct synth *synth) {
  (void)synth;
  return ASSIGN_VOICE_GAIN;
}

/*
* Assign the event handlers, scaler etc for polyphonic voice mode.
*/
const struct assigner poly_assigner = {.name = "poly",
                                       .key_down = poly_key_down,
                                       .key_up = poly_key_up,
                                       .gate_off = poly_gate_off,
                                       .all_off = poly_all_off,
                                       .update = poly_update,
                                       .mix_scale = poly_mix_scale};
