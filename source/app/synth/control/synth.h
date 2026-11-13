/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
*/
#ifndef FRUGI_SYNTH_H
#define FRUGI_SYNTH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "assign.h"
#include "chorus.h"
#include "keys.h"
#include "param.h"
#include "patch_store.h"
#include "voice.h"

/*
 * Careful changing this - see synth_render() for unrolled code
 * which needs to be kept in sync with this value.
 */
#ifndef SYNTH_MAX_VOICES
#define SYNTH_MAX_VOICES (6)
#endif

/*
 * Resolves a user patch slot to parameters, injected at synth_init() time
 * so MIDI Program Change can load one without synth.c itself depending
 * on the EEPROM-backed patch store (patch_persist.h) - keeps this module
 * host-testable. The signature matches patch_persist_load() exactly, so
 * the real build passes that straight through with no wrapper; tests can
 * pass NULL (Program Change then always falls back to the default patch)
 * or a small fake.
 */
typedef bool (*synth_patch_load_fn)(uint16_t slot,
                                    param_value_t params[PARAM_COUNT],
                                    char name_out[PATCH_NAME_LEN]);

/*
 * The synth holds all voices and the shared parameter array.
 *
 * Sample memory is statically allocated as a contiguous block, with
 * each voice receiving a pointer to its slice.
 */
struct synth {
  uint8_t midi_channel;
  synth_patch_load_fn patch_load; /* may be NULL - see synth_patch_load_fn */

  float sample_blocks[SYNTH_MAX_VOICES * AUDIO_BLOCK_SIZE];

  param_value_t
      params[PARAM_COUNT]; /* global parameter array (enum, int32 or float) */

  struct voice voice[SYNTH_MAX_VOICES];
  struct chorus chorus; /* Stereo widening after the voice summing mixer */

  /*
   * Voice management (design-voice-architecture.md): the keys layer tracks
   * what the player is doing, the assigner for the current voice mode turns
   * that into voice commands.
   */
  struct keys keys;
  const struct assigner *assigner; /* poly_assigner or mono_assigner */
  enum voice_mode mode;            /* POLY or MONO - selects the assigner */

  /* Poly assigner state: per-voice trigger order, for "oldest" */
  uint32_t trigger_seq[SYNTH_MAX_VOICES];
  uint32_t trigger_counter;

  /* Mono assigner state: the note all voices are sounding (or KEYS_NONE) */
  uint8_t mono_note;
  bool unison_in_phase; /* Voices started together with no detune */

  bool portamento_enable;
  float portamento_time;
  float unison_detune; /* Unison detune spread in cents (0-100), mono mode */

  /* Live performance controller state - not part of a patch */
  float pitch_bend_semitones;
  float mod_wheel;
  bool chorus_enable; /* Stereo chorus on summed mix*/
};

/* Public API */
void synth_init(struct synth *synth, const uint8_t midi_channel,
                synth_patch_load_fn patch_load);
void synth_render(struct synth *synth, float *left, float *right);
void synth_handle_midi(struct synth *synth, uint8_t byte0, uint8_t byte1,
                       uint8_t byte2);
void synth_set_param(struct synth *synth, uint8_t param_id,
                     param_value_t value);

/* Installs an already-resolved patch wholesale (eg. loaded from a user
   slot) and refreshes voice/performance params - used by the MIDI
   Program Change handler below, and by the UI's patch page once it has
   params to install. */
void synth_load_patch(struct synth *synth,
                      const param_value_t params[PARAM_COUNT]);

#endif /* FRUGI_SYNTH_H */
