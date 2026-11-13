/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   This module handles synthesiser parameters (user controls).

   It does not handle mapping them to MIDI CC message, nor does it handle
   organising them into patches.  This is just the low-level parameters, their
   types and identifiers etc.

   It includes a number of helper routines used to map physical controls to
   values to cater for different taper types (log, linear, quad etc).

   The main configuration item here is the type descriptor table which defines
   the type (float, int, bool, enum) of each parameter.  This is used by
   other modules for display,stepping or updating the values.

   Params have no concept of the value they hold - no range checks nor any
   indicator of any conversion function to apply.  That is the concern of the
   receiving DSP module.
   -----------------------------------------------------------------------------
*/
#include "param.h"

/*
 * This creates a small cache of parameter smoothing coefficients, the same time
 * constant is often used for multiple parameters in multiple modules and these
 * only need to be calculated once for all voice instances.  Centralising the
 * function here also avoids repeating the same code across modules.
 */

float param_smooth_coeff(float time_ms) {
  static float times[PARAM_SMOOTH_COEFF_CACHE_SIZE];
  static float coeffs[PARAM_SMOOTH_COEFF_CACHE_SIZE];
  static uint8_t count = 0;

  for (uint8_t i = 0; i < count; i++) {
    if (times[i] == time_ms) {
      return coeffs[i];
    }
  }

  const float smooth_blocks =
      fmaxf(time_ms * (AUDIO_SAMPLE_RATE / 1000.0f) / AUDIO_BLOCK_SIZE, 1.0f);
  const float coeff = 1.0f - expf(-1.0f / smooth_blocks);

  /*
   * Cache it if there is room.  When the cache is full the value is still
   * correct, just recomputed on every call - never written past the end of
   * the arrays (an assert alone would be compiled out of release builds).
   */
  if (count < PARAM_SMOOTH_COEFF_CACHE_SIZE) {
    times[count] = time_ms;
    coeffs[count] = coeff;
    count++;
  }

  return coeff;
}

static bool param_changed;

bool get_param_changed() { return param_changed; }

void set_param_changed(bool has_changed) { param_changed = has_changed; }

/*
 * Parameter descriptor table.  This describes the name and other details of
 * a parameter.
 */
const struct param_desc param_desc[PARAM_COUNT] = {
    [DCA_LEVEL] = {.type = PARAM_TYPE_FLOAT},
    [DCA_MOD_SOURCE] = {.type = PARAM_TYPE_ENUM,
                        .enum_count = MOD_SOURCE_COUNT},
    [DCA_MOD_DEPTH] = {.type = PARAM_TYPE_FLOAT},
    [DCA_ATTACK] = {.type = PARAM_TYPE_FLOAT},
    [DCA_DECAY] = {.type = PARAM_TYPE_FLOAT},
    [DCA_SUSTAIN] = {.type = PARAM_TYPE_FLOAT},
    [DCA_RELEASE] = {.type = PARAM_TYPE_FLOAT},
    [DCA_NOTE_TRACK] = {.type = PARAM_TYPE_BOOL},
    [DCA_VELOCITY_TRACK] = {.type = PARAM_TYPE_BOOL},

    [DCO1_WAVE] = {.type = PARAM_TYPE_ENUM, .enum_count = DCO_WAVE_COUNT},
    [DCO1_LEVEL] = {.type = PARAM_TYPE_FLOAT},
    [DCO1_OCTAVE] = {.type = PARAM_TYPE_INT,
                     .min = -OSC_OCTAVE_RANGE,
                     .max = OSC_OCTAVE_RANGE},
    [DCO1_SEMI] = {.type = PARAM_TYPE_INT,
                   .min = -OSC_SEMI_RANGE,
                   .max = OSC_SEMI_RANGE},
    [DCO1_CENTS] = {.type = PARAM_TYPE_FLOAT},
    [DCO1_PULSEWIDTH] = {.type = PARAM_TYPE_FLOAT},
    [DCO1_MOD_SOURCE] = {.type = PARAM_TYPE_ENUM,
                         .enum_count = MOD_SOURCE_COUNT},
    [DCO1_MOD_DEPTH] = {.type = PARAM_TYPE_FLOAT},
    [DCO1_PWM_SOURCE] = {.type = PARAM_TYPE_ENUM,
                         .enum_count = MOD_SOURCE_COUNT},
    [DCO1_PWM_DEPTH] = {.type = PARAM_TYPE_FLOAT},

    [DCO2_WAVE] = {.type = PARAM_TYPE_ENUM, .enum_count = DCO_WAVE_COUNT},
    [DCO2_LEVEL] = {.type = PARAM_TYPE_FLOAT},
    [DCO2_OCTAVE] = {.type = PARAM_TYPE_INT,
                     .min = -OSC_OCTAVE_RANGE,
                     .max = OSC_OCTAVE_RANGE},
    [DCO2_SEMI] = {.type = PARAM_TYPE_INT,
                   .min = -OSC_SEMI_RANGE,
                   .max = OSC_SEMI_RANGE},
    [DCO2_CENTS] = {.type = PARAM_TYPE_FLOAT},
    [DCO2_PULSEWIDTH] = {.type = PARAM_TYPE_FLOAT},
    [DCO2_MOD_SOURCE] = {.type = PARAM_TYPE_ENUM,
                         .enum_count = MOD_SOURCE_COUNT},
    [DCO2_MOD_DEPTH] = {.type = PARAM_TYPE_FLOAT},
    [DCO2_PWM_SOURCE] = {.type = PARAM_TYPE_ENUM,
                         .enum_count = MOD_SOURCE_COUNT},
    [DCO2_PWM_DEPTH] = {.type = PARAM_TYPE_FLOAT},

    [NOISE_LEVEL] = {.type = PARAM_TYPE_FLOAT},

    [DCO_DRIVE] = {.type = PARAM_TYPE_FLOAT},
    [DCO_RINGMOD_LEVEL] = {.type = PARAM_TYPE_FLOAT},

    [DCF_MODE] = {.type = PARAM_TYPE_ENUM, .enum_count = FILTER_COUNT},
    [DCF_CUTOFF] = {.type = PARAM_TYPE_FLOAT},
    [DCF_RES] = {.type = PARAM_TYPE_FLOAT},
    [DCF_SAT] = {.type = PARAM_TYPE_FLOAT},
    [DCF_TRACK] = {.type = PARAM_TYPE_BOOL},
    [DCF_MOD_SOURCE] = {.type = PARAM_TYPE_ENUM,
                        .enum_count = MOD_SOURCE_COUNT},
    [DCF_MOD_DEPTH] = {.type = PARAM_TYPE_FLOAT},

    [ENV1_MODE] = {.type = PARAM_TYPE_ENUM, .enum_count = ENV_MODE_COUNT},
    [ENV1_ATTACK] = {.type = PARAM_TYPE_FLOAT},
    [ENV1_DECAY] = {.type = PARAM_TYPE_FLOAT},
    [ENV1_SUSTAIN] = {.type = PARAM_TYPE_FLOAT},
    [ENV1_RELEASE] = {.type = PARAM_TYPE_FLOAT},

    [LFO1_RATE] = {.type = PARAM_TYPE_FLOAT},
    [LFO1_MODE] = {.type = PARAM_TYPE_ENUM, .enum_count = LFO_MODE_COUNT},

    [SYNTH_VOICE_MODE] = {.type = PARAM_TYPE_ENUM,
                          .enum_count = VOICE_MODE_COUNT},
    [SYNTH_PORTAMENTO_ON] = {.type = PARAM_TYPE_BOOL},
    [SYNTH_PORTAMENTO_TIME] = {.type = PARAM_TYPE_FLOAT},
    [SYNTH_UNISON_DETUNE] = {.type = PARAM_TYPE_FLOAT},
    [SYNTH_BEND_RANGE] = {.type = PARAM_TYPE_FLOAT},
    [SYNTH_MOD_WHEEL_DEST] = {.type = PARAM_TYPE_ENUM,
                              .enum_count = MOD_WHEEL_DEST_COUNT},
    [SYNTH_CHORUS_ON] = {.type = PARAM_TYPE_BOOL}};
