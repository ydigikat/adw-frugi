/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
*/
#ifndef FRUGI_DCF_H
#define FRUGI_DCF_H

#include "param.h"

/*
 * Cutoff range in Hz.  Modulation and key tracking are clamped to this range
 * (see dcf_calculate_coefficients()).
 */
#define DCF_CUTOFF_MIN_HZ (80.0f)
#define DCF_CUTOFF_MAX_HZ (18000.0f)

enum filter_tap_col { A_TAP = 0, B_TAP, C_TAP, D_TAP, E_TAP };

/*
 * One TPT one-pole stage.
 *
 * alpha - the TPT feed-forward coefficient (same value for all stages
 *         is set from cutoff at each block).
 * beta  - this stage's add to the feedforward sum ('sigma' in filter.c).
 * z1    - the TPT integrator's stored state.
 */
struct sub_filter {
  float alpha;
  float beta;
  float a0;
  float z1;
};

struct dcf {
  /* User parameters */
  enum filter_mode filter_mode_param;
  float cutoff_param;       
  float target_cutoff_param;                            
  float resonance_param;
  float saturation_param;
  bool note_tracking_param;

  /* Private data */
  float cutoff;
  float resonance;
  float bass_comp;
  float alpha0; 
  float smooth_coeff; 
  uint8_t note;

  /* Filter cascade */
  struct sub_filter f1, f2, f3, f4;
};

void dcf_init(struct dcf *dcf);
void dcf_reset(struct dcf *dcf);
void dcf_note_on(struct dcf *dcf, uint8_t note);
void dcf_render(struct dcf *dcf, float sample_block[], float cutoff_mod);
void dcf_update(struct dcf *dcf, enum filter_mode mode, float cutoff,
                float resonance, float saturation, bool note_tracking);

#endif /* FRUGI_DCF_H */
