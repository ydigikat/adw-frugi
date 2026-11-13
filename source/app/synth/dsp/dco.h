/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
*/

#ifndef FRUGI_DCO_H
#define FRUGI_DCO_H

#include "param.h"

/*
 * Ring-mod output level at full cross-mod, relative to a single full-scale
 * oscillator at the DCO headroom.  The product of two +/-1 saw/tri waves has
 * an RMS of ~1/3, close to a lone saw or tri at its waveform gain, so 1.0
 * makes the ring about as loud as the DCO2 it replaces (pulse x pulse is a
 * +/-1 square and comes out hotter).
 */
#define DCO_RING_GAIN (1.0f)

struct dco {
  enum dco_wave waveform;
  float pulse_width;
  float pulse_width_base;
  float level;     /* render amplitude: headroom x waveform gain */
  float mix_level; /* DCOx_LEVEL setting, applied by dco_mix() */
  float tuning;
  float pitch_bend_semitones;
  float phase;
  float inc;
  float pitch;
  float gain;
};

/* API */
void dco_init(struct dco *dco, float gain);
void dco_reset(struct dco *dco);
void dco_render(struct dco *dco, float sample_block[], float pitch_mod,
                float pwm_mod);
void dco_mix(float dst_block[], const float dco1_block[],
             const float dco2_block[], const struct dco *dco1,
             const struct dco *dco2, float xmod);
float dco_unit_scale(const struct dco *dco);
void dco_update(struct dco *dco, float level, enum dco_wave waveform,
                int32_t octave, int32_t semitones, float fine_tune,
                float pulse_width);
void dco_set_bend(struct dco *dco, float pitch_bend_semitones);
void dco_set_phase(struct dco *dco, float phase);
void dco_note_on(struct dco *dco, float pitch, bool is_legato);
void dco_note_off(struct dco *dco);

#endif /* FRUGI_DCO_H*/
