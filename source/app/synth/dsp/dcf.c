/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   This is a ladder style filter with multiple taps allowing 2-pole and 4-pole
   LP,BP and HP modes.  The filter is resonant (to an extent) and provides a 
   saturator (drive) to tame any wild resonant peaks.

   It is not a sophisticated filter - but functional and fit for purpose.

   Jump Tables.  The filter uses jump tables to avoid conditional logic (which
   is slow).  This can seem a little convoluted.

   There are 3 groups of functions used in the table:
   - Filter function - does the actual filter calculation.
   - Output function - picks the filter output taps to use.
   - Drive function  - applies an optional drive function.

   Each filter setup is a single combination of these 3 functions.  
   Take a look at the filter_funcs[] table for usage.  This method allows each
   filter mode to be defined as a set of 3 functions which are then called 
   directly indexed on the filter MODE_TYPE etc.
   -----------------------------------------------------------------------------
*/

#include "dcf.h"
#include "trace.h"

/* Ranges for control mapping/smoothing */
static const float FILTER_CUT_MIN = DCF_CUTOFF_MIN_HZ;
static const float FILTER_CUT_MAX = DCF_CUTOFF_MAX_HZ;
static const float FILTER_RES_MIN = 0.0f;
static const float FILTER_RES_MAX = 4.0f;
static const float FILTER_SAT_MIN = 0.0f;
static const float FILTER_SAT_MAX = 5.0f;
static const float FILTER_MOD_MAX = 4.0f;
static const float PARAM_SMOOTH_TIME_MS = 10.0f;
static const float FILTER_PI = 3.14159265358979323846f;
static const float FILTER_KEYTRACK_REF_NOTE = 60.0f;

void dcf_init(struct dcf *dcf) {
  RTT_ASSERT(dcf != NULL);

  dcf->cutoff = FILTER_CUT_MAX;
  dcf->alpha0 = 1.0f;
  dcf->bass_comp = 1.0f;

  dcf->smooth_coeff = param_smooth_coeff(PARAM_SMOOTH_TIME_MS);

  dcf->f1.alpha = 1.0f;
  dcf->f1.beta = 0.0f;
  dcf->f1.z1 = 0.0f;
  dcf->f1.a0 = 1.0f;

  dcf->f2.alpha = 1.0f;
  dcf->f2.beta = 0.0f;
  dcf->f2.z1 = 0.0f;
  dcf->f2.a0 = 1.0f;

  dcf->f3.alpha = 1.0f;
  dcf->f3.beta = 0.0f;
  dcf->f3.z1 = 0.0f;
  dcf->f3.a0 = 1.0f;

  dcf->f4.alpha = 1.0f;
  dcf->f4.beta = 0.0f;
  dcf->f4.z1 = 0.0f;
  dcf->f4.a0 = 1.0f;

  dcf_reset(dcf);
}

void dcf_reset(struct dcf *dcf) {
  RTT_ASSERT(dcf != NULL);

  dcf->f1.z1 = 0.0f;
  dcf->f2.z1 = 0.0f;
  dcf->f3.z1 = 0.0f;
  dcf->f4.z1 = 0.0f;
}

/*
 * Fast tangent calculation, this can make an audible difference to the
 * way the filter sounds so use at own peril!
 */
static inline float dcf_fast_tan(float x) {
  const float x2 = x * x;
  return x / (1.0f - 0.28f * x2);
}

/*
 * Filter is block based like most other DSP modules.
 */
static void dcf_calculate_coefficients(struct dcf *dcf, float cutoff_mod) {
  dcf->cutoff_param +=
      (dcf->target_cutoff_param - dcf->cutoff_param) * dcf->smooth_coeff;

  /* Filter keytracking, increase filter effect as pitch increases */
  float keytrack =
      dcf->note_tracking_param
          ? powf(2.0f, (dcf->note - FILTER_KEYTRACK_REF_NOTE) / 12.0f)
          : 1.0f;

  dcf->cutoff = dcf->cutoff_param * powf(FILTER_MOD_MAX, cutoff_mod) * keytrack;

  /*
   * Clamp modulation and key tracking to the filter range, at the nearer
   * limit: a sweep below the minimum holds the filter closed at the minimum,
   * one above the maximum holds it open.
   */
  dcf->cutoff = fminf(fmaxf(dcf->cutoff, FILTER_CUT_MIN), FILTER_CUT_MAX);

  // float g = tanf(FILTER_PI * dcf->cutoff / AUDIO_SAMPLE_RATE);
  float g = dcf_fast_tan(FILTER_PI * dcf->cutoff / AUDIO_SAMPLE_RATE);
  float G = g / (1.0f + g);

  dcf->f1.alpha = G;
  dcf->f2.alpha = G;
  dcf->f3.alpha = G;
  dcf->f4.alpha = G;

  dcf->f1.beta = G * G * G / (1.0f + g);
  dcf->f2.beta = G * G / (1.0f + g);
  dcf->f3.beta = G / (1.0f + g);
  dcf->f4.beta = 1.0f / (1.0f + g);

  float gamma = G * G * G * G;
  dcf->alpha0 = 1.0f / (1.0f + dcf->resonance_param * gamma);
}

/*
* These 3 function types combine as a set for each filter mode.  They
* populate a jump table which can then call the functions directly indexed
* on the filter mode.
*/
typedef void (*output_func)(float *output, float U, float lpf1, float lpf2,
                            float lpf3, float lpf4);
typedef void (*filter_func)(struct dcf *, float sample_block[],
                            output_func select_taps);
typedef float (*drive_func)(float U, float saturate);

/*
* The jump table.  Filter is the filter function, output function selects
* the output taps for a specific mode (LP2, LP4 etc)
*/
struct jump_table_entry {
  filter_func filter;
  output_func output;
};

/*
 * Output tap selectors.
 */
static void output_lpf2(float *output, float U, float lpf1, float lpf2,
                        float lpf3, float lpf4) {
  *output = lpf1;
}

static void output_bpf2(float *output, float U, float lpf1, float lpf2,
                        float lpf3, float lpf4) {
  *output = lpf1 * 2.0f + lpf2 * -2.0f;
}

static void output_hpf2(float *output, float U, float lpf1, float lpf2,
                        float lpf3, float lpf4) {
  *output = U + lpf1 * -2.0f + lpf2;
}

static void output_lpf4(float *output, float U, float lpf1, float lpf2,
                        float lpf3, float lpf4) {
  *output = lpf4;
}

static void output_bpf4(float *output, float U, float lpf1, float lpf2,
                        float lpf3, float lpf4) {
  *output = lpf2 * 4.0f + lpf3 * -8.0f + lpf4 * 4.0f;
}

static void output_hpf4(float *output, float U, float lpf1, float lpf2,
                        float lpf3, float lpf4) {
  *output = U + lpf1 * -4.0f + lpf2 * 6.0f + lpf3 * -4.0f + lpf4;
}

/*
* Darkest of dark magic - function for a fast reciprocal. I've not made 
* any attempt to personally unpick this one!
*/
static inline float dcf_fast_recip(float x) {
  union {
    float f;
    uint32_t i;
  } u;

  u.f = x;
  u.i = 0x7EF311C2u - u.i;
  float y = u.f;
  y = y * (2.0f - x * y);
  return y;
}

/* Input drive  */
static inline float dcf_saturate_drive(float U, float saturate) {
#ifdef FILTER_DRIVE
  /* Gain starts at unity and grows with saturation, low drive settings
     fade in */
  float x = U * (1.0f + saturate);

  if (x > 3.0f) {
    x = 3.0f;
  } else if (x < -3.0f) {
    x = -3.0f;
  }

  float x2 = x * x;
  return x * (27.0f + x2) * dcf_fast_recip(27.0f + 9.0f * x2);
#else
  return U;
#endif
}

/* or not */
static inline float dcf_clean_drive(float U, float saturate) {
  (void)saturate;
  return U;
}

/*
 * Single-pass render for all 6 filter modes.  The functions defined
 * in the jump table for this mode index choose which output taps to use.
 */
static void internal_render(struct dcf *dcf, float sample_block[],
                            output_func select_taps) {
  RTT_ASSERT(dcf);
  RTT_ASSERT(sample_block);
  RTT_ASSERT(select_taps);

  /* Stack cache variables */
  const float alpha0 = dcf->alpha0;
  const float resonance = dcf->resonance_param;
  const float saturate = dcf->saturation_param;

  drive_func drive = (saturate > 0 ? dcf_saturate_drive : dcf_clean_drive);

  float z1 = dcf->f1.z1;
  float z2 = dcf->f2.z1;
  float z3 = dcf->f3.z1;
  float z4 = dcf->f4.z1;

  const float alpha1 = dcf->f1.alpha;
  const float alpha2 = dcf->f2.alpha;
  const float alpha3 = dcf->f3.alpha;
  const float alpha4 = dcf->f4.alpha;

  const float beta1 = dcf->f1.beta;
  const float beta2 = dcf->f2.beta;
  const float beta3 = dcf->f3.beta;
  const float beta4 = dcf->f4.beta;

  const float gamma = alpha1 * alpha1 * alpha1 * alpha1;

  float *restrict ptr = sample_block;
  const float *restrict end = ptr + AUDIO_BLOCK_SIZE;

  while (ptr < end) {
    float sigma = beta1 * z1 + beta2 * z2 + beta3 * z3 + beta4 * z4;

    float U = *ptr - resonance * (gamma * (*ptr) + sigma) * alpha0;

    U = drive(U, saturate);

    float vn = (U - z1) * alpha1;
    float lpf1 = vn + z1;
    z1 = vn + lpf1;

    vn = (lpf1 - z2) * alpha2;
    float lpf2 = vn + z2;
    z2 = vn + lpf2;

    vn = (lpf2 - z3) * alpha3;
    float lpf3 = vn + z3;
    z3 = vn + lpf3;

    vn = (lpf3 - z4) * alpha4;
    float lpf4 = vn + z4;
    z4 = vn + lpf4;

    select_taps(ptr++, U, lpf1, lpf2, lpf3, lpf4);
  }

  dcf->f1.z1 = z1;
  dcf->f2.z1 = z2;
  dcf->f3.z1 = z3;
  dcf->f4.z1 = z4;
}

static const struct jump_table_entry filter_funcs[] = {
    {internal_render, output_lpf2}, {internal_render, output_bpf2},
    {internal_render, output_hpf2}, {internal_render, output_lpf4},
    {internal_render, output_bpf4}, {internal_render, output_hpf4}};

void dcf_render(struct dcf *dcf, float sample_block[], float cutoff_mod) {
  RTT_ASSERT(dcf != NULL);
  RTT_ASSERT(sample_block != NULL);

  dcf_calculate_coefficients(dcf, cutoff_mod);
  filter_funcs[dcf->filter_mode_param].filter(
      dcf, sample_block, filter_funcs[dcf->filter_mode_param].output);
}

void dcf_update(struct dcf *dcf, enum filter_mode mode, float cutoff,
                float resonance, float saturation, bool note_tracking) {
  RTT_ASSERT(dcf != NULL);

  dcf->filter_mode_param = mode;
  dcf->target_cutoff_param =
      param_uni_to_exp(cutoff, FILTER_CUT_MIN, FILTER_CUT_MAX);
  dcf->resonance_param =
      param_uni_to_linear(resonance, FILTER_RES_MIN, FILTER_RES_MAX);
  dcf->saturation_param =
      param_uni_to_linear(saturation, FILTER_SAT_MIN, FILTER_SAT_MAX);
  dcf->note_tracking_param = note_tracking;
}


void dcf_note_on(struct dcf *dcf, uint8_t note) {
  RTT_ASSERT(dcf != NULL);

  dcf->note = note;
  dcf->cutoff_param = dcf->target_cutoff_param;
}