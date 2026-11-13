/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
*/
#ifndef FRUGI_VOICE_H
#define FRUGI_VOICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dca.h"
#include "dcf.h"
#include "dco.h"
#include "env.h"
#include "lfo.h"
#include "mod.h"
#include "noise.h"
#include "param.h"
#include "shape.h"

/*
 * Fixed mod routing table.
 */
#define MAX_MOD_SLOTS (6)

/*
 * Mod wheel scale at full wheel deflection.
 */
#define MOD_WHEEL_DEPTH_MAX (1.0f)

/*
 * Voice lifecycle states.
 *
 * VOICE_IDLE:     Free, ready for allocation
 * VOICE_ACTIVE:   In use, can be stolen if needed
 * VOICE_STEALING: Mid-steal transition, protected from re-stealing
 */
enum voice_state { VOICE_IDLE, VOICE_ACTIVE, VOICE_STEALING };

/*
 * Voice instance data.
 */
struct voice {
  uint8_t idx;

  /* audio/modulator/parameter data */
  float *sample_block;
  float mod_sources[MOD_SOURCE_COUNT]; /* modulation bus - written by LFO/ENV
                                          each block */
  float mod_dest[MOD_DEST_COUNT];      /* routed modulation - written by
                                          mod_matrix_render() */
  struct mod_slot
      slots[MAX_MOD_SLOTS]; /* patch cords, rebuilt in voice_update_chain() */
  float dca_env_level;
  const param_value_t *params;

  /* Lifecycle management data */
  enum voice_state state;
  bool update_pending; /* Deferred to the next render - see voice_update() */
  bool gate;           /* High while the note should sound (key or pedal) */

  /* Active (playing) note data */
  uint8_t note;
  uint8_t velocity;
  float pitch; /* note frequency - detune and bend are applied on top */

  /* The note a steal hands over to once the old note has faded out */
  uint8_t target_note;
  uint8_t target_velocity;
  float target_pitch;

  /*
   * Performance controller state.  Set by the synth / voice assigner; a
   * change takes effect at the next render after voice_update().
   */
  float unison_cents;         /* Unison detune offset - applied live, like bend */
  float start_phase;          /* Oscillator start phase 0..1, or <0 for default */
  float pitch_bend_semitones; /* Current pitch bend */
  float mod_wheel;            /* Current mod wheel position, 0..1 */

  /*
   * Portamento (mono legato glide) - portamento_coeff is computed once at
   * the synth level (identical for every voice) and broadcast here.
   */
  bool portamento_enable;
  float portamento_coeff; /* Per-block one-pole approach coefficient */
  float glide_pitch;      /* Currently-sounding, glide-smoothed pitch */

  /*
   * Voice signal chain modules.
   *
   * This is a typical virtual analogue signal chain:
   * DCO1 + DCO2 + Noise -> Shaper -> DCF -> DCA
   *
   * With amplitude and filter/modulation envelopes and a single LFO. Nothing
   * Earth-shattering here.
   *
   * The ordering in the struct here is not significant.
   */
  struct dco dco_1;
  struct dco dco_2;
  struct noise noise;
  struct shaper shaper;
  struct dcf dcf;
  struct lfo lfo1;
  struct env amp_env;
  struct env mod_env;
  struct dca dca;
};

/*
 * Public API - gate, trigger and pitch (design-voice-architecture.md §6).
 * Every call is valid in every voice state.
 */
void voice_init(struct voice *voice, float sample_block[],
                const param_value_t params[]);
void voice_render(struct voice *voice);
void voice_update(struct voice *voice);

void voice_trigger(struct voice *voice, uint8_t midi_note,
                   uint8_t midi_velocity);
void voice_set_note(struct voice *voice, uint8_t midi_note);
void voice_gate_off(struct voice *voice);
void voice_kill(struct voice *voice);

#endif /* FRUGI_VOICE_H */
