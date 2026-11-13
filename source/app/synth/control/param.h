/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
*/
#ifndef FRUGI_PARAM_DESC_H
#define FRUGI_PARAM_DESC_H

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "trace.h"

#define MAX_NAME_LEN (16)

/*
 * The parameter model: what a parameter is (id, type, range) and the
 * generic value-mapping math shared by consumers (MIDI CC decode,
 * UI display). No MIDI or patch-storage knowledge.
 */

/* Parameter ranges */
#define OSC_OCTAVE_RANGE (2)
#define OSC_SEMI_RANGE (6)
#define OSC_CENTS_RANGE (50)
#define ENV_TIME_MIN (0)
#define ENV_TIME_MAX (10000)
#define PORTAMENTO_TIME_MAX (5000)
#define UNISON_DETUNE_MAX (100) /* cents, squared curve - see synth.c */
#define BEND_RANGE_MAX (24)

/*
 * PARAM_TYPE_UNDEFINED is deliberately zero: a parameter missing from the
 * param_desc[] table is zero-filled, and would otherwise silently look like a
 * valid float.  test_param.c checks every parameter has a real type.
 */
enum param_type {
  PARAM_TYPE_UNDEFINED,
  PARAM_TYPE_FLOAT,
  PARAM_TYPE_ENUM,
  PARAM_TYPE_BOOL,
  PARAM_TYPE_INT
};

struct param_desc {
  enum param_type type;
  uint32_t enum_count; /* type == PARAM_TYPE_ENUM */
  int32_t min;         /* ftype == PARAM_TYPE_INT */
  int32_t max;         /* type == PARAM_TYPE_INT */
};

typedef union {
  float f;
  int32_t i;
  uint32_t e;
  bool b;
} param_value_t;

/* Parameters */
enum param_id {
  /* Voice parameters */
  DCA_LEVEL,
  DCA_MOD_SOURCE,
  DCA_MOD_DEPTH,
  DCA_ATTACK,
  DCA_DECAY,
  DCA_SUSTAIN,
  DCA_RELEASE,
  DCA_NOTE_TRACK,
  DCA_VELOCITY_TRACK,

  DCO1_WAVE,
  DCO1_LEVEL,
  DCO1_OCTAVE,
  DCO1_SEMI,
  DCO1_CENTS,
  DCO1_PULSEWIDTH,
  DCO1_MOD_SOURCE,
  DCO1_MOD_DEPTH,
  DCO1_PWM_SOURCE,
  DCO1_PWM_DEPTH,

  DCO2_WAVE,
  DCO2_LEVEL,
  DCO2_OCTAVE,
  DCO2_SEMI,
  DCO2_CENTS,
  DCO2_PULSEWIDTH,
  DCO2_MOD_SOURCE,
  DCO2_MOD_DEPTH,
  DCO2_PWM_SOURCE,
  DCO2_PWM_DEPTH,

  NOISE_LEVEL,

  DCO_DRIVE,
  DCO_RINGMOD_LEVEL,

  DCF_MODE,
  DCF_CUTOFF,
  DCF_RES,
  DCF_SAT,
  DCF_TRACK,
  DCF_MOD_SOURCE,
  DCF_MOD_DEPTH,

  ENV1_MODE,
  ENV1_ATTACK,
  ENV1_DECAY,
  ENV1_SUSTAIN,
  ENV1_RELEASE,

  LFO1_RATE,
  LFO1_MODE,

  /* End of voice params marker */
  VOICE_PARAM_COUNT,

  /* Global parameters */
  SYNTH_VOICE_MODE,
  SYNTH_PORTAMENTO_ON,
  SYNTH_PORTAMENTO_TIME,
  SYNTH_UNISON_DETUNE,
  SYNTH_BEND_RANGE,
  SYNTH_MOD_WHEEL_DEST,
  SYNTH_CHORUS_ON,

  PARAM_COUNT
};

enum dco_wave { DCO_TRI, DCO_SAW, DCO_PULSE, DCO_WAVE_COUNT };

enum env_mode {
  ENV_NORMAL,
  ENV_BIASED,
  ENV_INVERTED,
  ENV_INVERTED_BIASED,
  ENV_MODE_COUNT
};

enum mod_source {
  LFO1_TRI,
  LFO1_SAW,
  LFO1_RAMP,
  LFO1_SQUARE,
  LFO1_SANDH,
  ENV1_LEVEL,

  MOD_SOURCE_COUNT
};

enum mod_wheel_dest {
  MOD_WHEEL_DEST_VCO,
  MOD_WHEEL_DEST_VCO_VCF,
  MOD_WHEEL_DEST_VCF,

  MOD_WHEEL_DEST_COUNT
};

enum lfo_mode {
  LFO_MODE_FREE,
  LFO_MODE_NOTE,

  LFO_MODE_COUNT
};

enum voice_mode { VOICE_MODE_POLY, VOICE_MODE_MONO, VOICE_MODE_COUNT };

enum filter_mode {
  FILTER_LPF2,
  FILTER_BPF2,
  FILTER_HPF2,
  FILTER_LPF4,
  FILTER_BPF4,
  FILTER_HPF4,
  FILTER_COUNT
};

/* The parameter type descriptor table */
extern const struct param_desc param_desc[PARAM_COUNT];

/*
 * Linear mapping from normalized value to specified range
 * Use for parameters where equal control increments should produce equal
 * value changes. Levels, mix amounts, and scalers.
 */
static inline float param_uni_to_linear(float u, float minv, float maxv) {
  return minv + u * (maxv - minv);
}

/*
 * Exponential mapping from normalized value to specified range
 * Use for parameters where human perception is logarithmic: frequencies,
 * dcf cutoff, and envelope times.
 * The curve provides fine control at low values, coarse control at high.
 */
static inline float param_uni_to_exp(float u, float minv, float maxv) {
  RTT_ASSERT(minv > 0);
  return minv * powf(maxv / minv, u);
}

/*
 * Quadratic mapping from normalized value to specified range.
 * Use where a control should feel biased toward its low end - fine
 * control near zero, coarse control near max - without the asymptotic
 * flatness of the exponential mapping above. Fits drive/overdrive-style
 * controls, where most of the useful range is near the bottom.
 */
static inline float param_uni_to_quadratic(float u, float minv, float maxv) {
  return minv + u * u * (maxv - minv);
}

/*
 * Convert linear value in range to normalized
 */
static inline float param_linear_to_uni(float val, float minv, float maxv) {
  return (val - minv) / (maxv - minv);
}

/*
 * Convert bipolar value to normalized
 */
static inline float param_bipolar_to_uni(float val, float range) {
  return (val / (2.0f * range)) + 0.5f;
}

/*
 * Number of distinct smoothing time constants cached (see param.c).  More
 * than this still work, they are just computed on every call.
 */
#ifndef PARAM_SMOOTH_COEFF_CACHE_SIZE
#define PARAM_SMOOTH_COEFF_CACHE_SIZE (4)
#endif

float param_smooth_coeff(float time_ms);
bool get_param_changed();
void set_param_changed(bool has_changed);

#endif /* FRUGI_PARAM_DESC_H */
