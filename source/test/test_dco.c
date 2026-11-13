/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   A.I. Disclosure:  These unit tests written by Anthropic Claude.
   -----------------------------------------------------------------------------
*/

#include "dco.h"
#include "unity.h"
#include <math.h>

#define FREQ_TEST_BLOCKS                                                       \
  (100) /* enough cycles to average out edge quantisation */

static struct dco dco;

/*
 * Renders `nblocks` blocks back to back into `buffer`, which must be at
 * least nblocks * AUDIO_BLOCK_SIZE samples.
 */
static void render_blocks(float *buffer, int nblocks) {
  for (int b = 0; b < nblocks; b++)
    dco_render(&dco, &buffer[b * AUDIO_BLOCK_SIZE], 0.0f, 0.0f);
}

static int count_zero_crossings(const float *buffer, int n) {
  int crossings = 0;
  for (int i = 1; i < n; i++) {
    if ((buffer[i - 1] < 0.0f && buffer[i] >= 0.0f) ||
        (buffer[i - 1] >= 0.0f && buffer[i] < 0.0f))
      crossings++;
  }
  return crossings;
}

/*
 * Estimates the fundamental frequency of a rendered signal by counting
 * zero crossings over a large number of blocks - not a golden-waveform
 * comparison, just enough to catch a broken phase increment.
 */
static float measure_frequency(int nblocks) {
  static float buffer[FREQ_TEST_BLOCKS * AUDIO_BLOCK_SIZE];
  render_blocks(buffer, nblocks);

  int crossings = count_zero_crossings(buffer, nblocks * AUDIO_BLOCK_SIZE);
  float duration = (float)(nblocks * AUDIO_BLOCK_SIZE) / AUDIO_SAMPLE_RATE;
  return (crossings / 2.0f) / duration;
}

void setUp() {
  dco = (struct dco){
      0}; /* dco_init() doesn't touch every field (e.g. waveform) */
}

void tearDown() {}

/* -------------------------------------------------------------------------
 * Initialisation / reset
 * ---------------------------------------------------------------------- */

void init_sets_expected_defaults(void) {
  dco_init(&dco, 0.8f);

  TEST_ASSERT_EQUAL_FLOAT(0.8f, dco.gain);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, dco.pitch);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, dco.inc);
}

void reset_sets_phase_for_triangle(void) {
  dco.waveform = DCO_TRI;
  dco.phase = 0.0f;

  dco_reset(&dco);

  TEST_ASSERT_EQUAL_FLOAT(0.5f, dco.phase);
}

void reset_sets_phase_for_non_triangle_waveforms(void) {
  dco.waveform = DCO_SAW;
  dco.phase = 0.9f;
  dco_reset(&dco);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, dco.phase);

  dco.waveform = DCO_PULSE;
  dco.phase = 0.3f;
  dco_reset(&dco);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, dco.phase);
}

/* -------------------------------------------------------------------------
 * Note on / off
 * ---------------------------------------------------------------------- */

void note_on_sets_pitch(void) {
  dco_init(&dco, 1.0f);
  dco_note_on(&dco, 440.0f, false);

  TEST_ASSERT_EQUAL_FLOAT(440.0f, dco.pitch);
}

void note_on_resets_phase_when_not_legato(void) {
  dco_init(&dco, 1.0f); /* waveform defaults to DCO_TRI (0) */
  dco.phase = 0.1f;

  dco_note_on(&dco, 440.0f, false);

  TEST_ASSERT_EQUAL_FLOAT(0.5f, dco.phase); /* triangle reset phase */
}

void note_on_preserves_phase_when_legato(void) {
  dco_init(&dco, 1.0f);
  dco.phase = 0.1f;

  dco_note_on(&dco, 440.0f, true);

  TEST_ASSERT_EQUAL_FLOAT(0.1f, dco.phase);
}

void note_off_clears_pitch(void) {
  dco_init(&dco, 1.0f);
  dco_note_on(&dco, 440.0f, false);

  dco_note_off(&dco);

  TEST_ASSERT_EQUAL_FLOAT(0.0f, dco.pitch);
}

/* -------------------------------------------------------------------------
 * Parameter update mapping
 * ---------------------------------------------------------------------- */

void update_scales_level_for_triangle(void) {
  dco_init(&dco, 1.0f);
  dco_update(&dco, 1.0f, DCO_TRI, 0, 0, 0.5f, 0.5f);

  TEST_ASSERT_EQUAL_FLOAT(0.65f,
                          dco.level); /* dco.c's private wave_gain[DCO_TRI] */
}

void update_scales_level_for_saw(void) {
  dco_init(&dco, 1.0f);
  dco_update(&dco, 1.0f, DCO_SAW, 0, 0, 0.5f, 0.5f);

  TEST_ASSERT_EQUAL_FLOAT(0.60f,
                          dco.level); /* dco.c's private wave_gain[DCO_SAW] */
}

void update_scales_level_for_pulse(void) {
  dco_init(&dco, 1.0f);
  dco_update(&dco, 1.0f, DCO_PULSE, 0, 0, 0.5f, 0.5f);

  TEST_ASSERT_EQUAL_FLOAT(0.45f,
                          dco.level); /* dco.c's private wave_gain[DCO_PULSE] */
}

void update_maps_pulse_width_linearly(void) {
  /* dco_update() writes the *base* width - dco.pulse_width itself is only
     computed (base + PWM) by dco_render(), see below. Expected bounds
     mirror dco.c's private DCO_PULSEWIDTH_MIN/MAX (0.05/0.95) */
  dco_update(&dco, 1.0f, DCO_PULSE, 0, 0, 0.5f, 0.0f);
  TEST_ASSERT_EQUAL_FLOAT(0.05f, dco.pulse_width_base);

  dco_update(&dco, 1.0f, DCO_PULSE, 0, 0, 0.5f, 0.5f);
  TEST_ASSERT_EQUAL_FLOAT(0.50f, dco.pulse_width_base);

  dco_update(&dco, 1.0f, DCO_PULSE, 0, 0, 0.5f, 1.0f);
  TEST_ASSERT_EQUAL_FLOAT(0.95f, dco.pulse_width_base);
}

/* -------------------------------------------------------------------------
 * PWM - live pulse_width is base + pwm_mod (the caller's precomputed
 * source*depth, resolved upstream by the mod matrix), recomputed every
 * dco_render() block and clamped to DCO_PULSEWIDTH_MIN/MAX.
 * ---------------------------------------------------------------------- */

void render_applies_pwm_to_live_pulse_width(void) {
  dco_init(&dco, 1.0f);
  dco_update(&dco, 1.0f, DCO_PULSE, 0, 0, 0.5f, 0.5f);
  dco_note_on(&dco, 220.0f, false);

  float block[AUDIO_BLOCK_SIZE];
  dco_render(&dco, block, 0.0f, 0.2f); /* pwm_mod = +0.2 width */

  TEST_ASSERT_EQUAL_FLOAT(0.7f, dco.pulse_width);
  TEST_ASSERT_EQUAL_FLOAT(0.5f,
                          dco.pulse_width_base); /* base is untouched by PWM */
}

void render_clamps_pwm_modulated_pulse_width(void) {
  dco_init(&dco, 1.0f);
  dco_update(&dco, 1.0f, DCO_PULSE, 0, 0, 0.5f, 0.9f);
  dco_note_on(&dco, 220.0f, false);

  float block[AUDIO_BLOCK_SIZE];
  dco_render(&dco, block, 0.0f,
             1.0f); /* base 0.9 + 1.0 would exceed DCO_PULSEWIDTH_MAX (0.95) */

  TEST_ASSERT_EQUAL_FLOAT(0.95f, dco.pulse_width);
}

void update_calculates_tuning_from_octave_semitones_cents(void) {
  /* fine tune 1.0 = +50 cents = +0.5 semitone */
  dco_update(&dco, 1.0f, DCO_TRI, 1, 2, 1.0f, 0.5f);

  /* tuning (octaves) = (octave*12 + semitones + cents/100) / 12 */
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, (12.0f + 2.0f + 0.5f) / 12.0f, dco.tuning);
}

/* C21: fine tune is +/-50 cents either side of the note, centre = in tune */
void fine_tune_centre_is_in_tune(void) {
  dco_update(&dco, 1.0f, DCO_TRI, 0, 0, 0.5f, 0.5f);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, dco.tuning);
}

void fine_tune_minimum_is_50_cents_flat(void) {
  dco_update(&dco, 1.0f, DCO_TRI, 0, 0, 0.0f, 0.5f);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, -0.5f / 12.0f, dco.tuning);
}

void fine_tune_maximum_is_50_cents_sharp(void) {
  dco_update(&dco, 1.0f, DCO_TRI, 0, 0, 1.0f, 0.5f);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.5f / 12.0f, dco.tuning);
}

/* One UI encoder step (0.01) is one cent */
void fine_tune_encoder_step_is_one_cent(void) {
  dco_update(&dco, 1.0f, DCO_TRI, 0, 0, 0.51f, 0.5f);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.01f / 12.0f, dco.tuning);
}

void update_calculates_tuning_with_negative_octave(void) {
  dco_update(&dco, 1.0f, DCO_TRI, -1, 0, 0.5f, 0.5f);

  TEST_ASSERT_EQUAL_FLOAT(-1.0f, dco.tuning);
}

/* -------------------------------------------------------------------------
 * Pitch bend
 * ---------------------------------------------------------------------- */

void set_bend_shifts_phase_increment_by_semitones(void) {
  dco_init(&dco, 1.0f);
  dco_update(&dco, 1.0f, DCO_TRI, 0, 0, 0.5f, 0.5f); /* tuning = 0 */
  dco_note_on(&dco, 440.0f, false);
  dco_set_bend(&dco, 12.0f); /* +1 octave */

  float block[AUDIO_BLOCK_SIZE];
  dco_render(&dco, block, 0.0f, 0.0f);

  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 440.0f * 2.0f / AUDIO_SAMPLE_RATE, dco.inc);
}

void zero_bend_leaves_phase_increment_unchanged(void) {
  dco_init(&dco, 1.0f);
  dco_update(&dco, 1.0f, DCO_TRI, 0, 0, 0.5f, 0.5f);
  dco_note_on(&dco, 440.0f, false);
  dco_set_bend(&dco, 0.0f);

  float block[AUDIO_BLOCK_SIZE];
  dco_render(&dco, block, 0.0f, 0.0f);

  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 440.0f / AUDIO_SAMPLE_RATE, dco.inc);
}

/* -------------------------------------------------------------------------
 * Render gating & phase increment
 * ---------------------------------------------------------------------- */

void render_is_noop_when_pitch_is_zero(void) {
  dco_init(&dco, 1.0f); /* pitch defaults to 0 - no note played */

  float block[AUDIO_BLOCK_SIZE];
  for (size_t i = 0; i < AUDIO_BLOCK_SIZE; i++)
    block[i] = 1234.5f;

  dco_render(&dco, block, 0.0f, 0.0f);

  for (size_t i = 0; i < AUDIO_BLOCK_SIZE; i++)
    TEST_ASSERT_EQUAL_FLOAT(1234.5f, block[i]);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, dco.inc);
}

void render_computes_phase_increment_from_pitch(void) {
  dco_init(&dco, 1.0f);
  dco_update(&dco, 1.0f, DCO_TRI, 0, 0, 0.5f, 0.5f); /* tuning = 0 */
  dco_note_on(&dco, 440.0f, false);

  float block[AUDIO_BLOCK_SIZE];
  dco_render(&dco, block, 0.0f, 0.0f);

  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 440.0f / AUDIO_SAMPLE_RATE, dco.inc);
}

void render_applies_modulation_to_phase_increment(void) {
  dco_init(&dco, 1.0f);
  dco_update(&dco, 1.0f, DCO_TRI, 0, 0, 0.5f, 0.5f);
  dco_note_on(&dco, 440.0f, false);

  float block[AUDIO_BLOCK_SIZE];
  dco_render(&dco, block, 1.0f, 0.0f); /* pitch_mod = +1 octave */

  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 440.0f * powf(2.0f, 1.0f) / AUDIO_SAMPLE_RATE,
                           dco.inc);
}

/* -------------------------------------------------------------------------
 * Waveform generation - closed-form first sample
 *
 * Each waveform starts from a fixed, known phase (set by dco_reset), so
 * the very first rendered sample can be computed by hand from the
 * generator's own formula. This catches sign/formula regressions in the
 * math without needing a golden reference waveform.
 * ---------------------------------------------------------------------- */

void triangle_first_sample_matches_closed_form(void) {
  dco_init(&dco, 1.0f);
  dco_update(&dco, 1.0f, DCO_TRI, 0, 0, 0.5f, 0.5f);
  dco_note_on(&dco, 440.0f, false);

  float block[AUDIO_BLOCK_SIZE];
  dco_render(&dco, block, 0.0f, 0.0f);

  /* Reset phase is 0.5 -> 2*|2*0.5-1|-1 = -1, so sample = -level */
  TEST_ASSERT_EQUAL_FLOAT(-0.65f, block[0]);
}

void saw_first_sample_matches_closed_form(void) {
  dco_init(&dco, 1.0f);
  dco_update(&dco, 1.0f, DCO_SAW, 0, 0, 0.5f, 0.5f);
  dco_note_on(&dco, 440.0f, false);

  float block[AUDIO_BLOCK_SIZE];
  dco_render(&dco, block, 0.0f, 0.0f);

  /* Reset phase is 0.0, where the rising-edge BLEP exactly cancels the
     naive -1 edge value, regardless of level */
  TEST_ASSERT_EQUAL_FLOAT(0.0f, block[0]);
}

void pulse_first_sample_matches_closed_form_at_symmetric_width(void) {
  dco_init(&dco, 1.0f);
  dco_update(&dco, 1.0f, DCO_PULSE, 0, 0, 0.5f, 0.5f); /* pw = 0.5 */
  dco_note_on(&dco, 440.0f, false);

  float block[AUDIO_BLOCK_SIZE];
  dco_render(&dco, block, 0.0f, 0.0f);

  /* At a symmetric 50% duty cycle, the DC offset is 0 and the phase-wrap
     BLEP cancels the naive +1 edge value at phase 0.0 */
  TEST_ASSERT_EQUAL_FLOAT(0.0f, block[0]);
}

/* -------------------------------------------------------------------------
 * Waveform generation - frequency tracking
 * ---------------------------------------------------------------------- */

void triangle_frequency_matches_requested_pitch(void) {
  dco_init(&dco, 1.0f);
  dco_update(&dco, 1.0f, DCO_TRI, 0, 0, 0.5f, 0.5f);
  dco_note_on(&dco, 1000.0f, false);

  TEST_ASSERT_FLOAT_WITHIN(10.0f, 1000.0f, measure_frequency(FREQ_TEST_BLOCKS));
}

void saw_frequency_matches_requested_pitch(void) {
  dco_init(&dco, 1.0f);
  dco_update(&dco, 1.0f, DCO_SAW, 0, 0, 0.5f, 0.5f);
  dco_note_on(&dco, 1000.0f, false);

  TEST_ASSERT_FLOAT_WITHIN(10.0f, 1000.0f, measure_frequency(FREQ_TEST_BLOCKS));
}

void pulse_frequency_matches_requested_pitch(void) {
  dco_init(&dco, 1.0f);
  dco_update(&dco, 1.0f, DCO_PULSE, 0, 0, 0.5f, 0.5f);
  dco_note_on(&dco, 1000.0f, false);

  TEST_ASSERT_FLOAT_WITHIN(10.0f, 1000.0f, measure_frequency(FREQ_TEST_BLOCKS));
}

/* -------------------------------------------------------------------------
 * Waveform generation - other invariants
 * ---------------------------------------------------------------------- */

void pulse_dc_offset_stays_near_zero_across_pulse_widths(void) {
  float pulse_widths[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};

  for (size_t p = 0; p < sizeof(pulse_widths) / sizeof(pulse_widths[0]); p++) {
    dco_init(&dco, 1.0f);
    dco_update(&dco, 1.0f, DCO_PULSE, 0, 0, 0.5f, pulse_widths[p]);
    dco_note_on(&dco, 220.0f, false);

    double sum = 0;
    float block[AUDIO_BLOCK_SIZE];
    const int nblocks = 50;
    for (int b = 0; b < nblocks; b++) {
      dco_render(&dco, block, 0.0f, 0.0f);
      for (size_t i = 0; i < AUDIO_BLOCK_SIZE; i++)
        sum += (double)block[i];
    }

    float mean = (float)(sum / (nblocks * AUDIO_BLOCK_SIZE));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, mean);
  }
}

void amplitude_stays_bounded_across_waveforms(void) {
  enum dco_wave waveforms[] = {DCO_TRI, DCO_SAW, DCO_PULSE};

  for (size_t w = 0; w < sizeof(waveforms) / sizeof(waveforms[0]); w++) {
    dco_init(&dco, 1.0f);
    dco_update(&dco, 1.0f, waveforms[w], 0, 0, 0.5f, 0.5f);
    dco_note_on(&dco, 1000.0f, false);

    float level = dco.level;
    float peak = 0.0f;
    float block[AUDIO_BLOCK_SIZE];
    const int nblocks = 20;
    for (int b = 0; b < nblocks; b++) {
      dco_render(&dco, block, 0.0f, 0.0f);
      for (size_t i = 0; i < AUDIO_BLOCK_SIZE; i++)
        peak = fmaxf(peak, fabsf(block[i]));
    }

    /* Small tolerance for BLEP ripple - this guards against gross
       regressions (broken gain, exploding BLEP coefficients, NaN, a
       reintroduced phase-wrap overshoot), not exact peak shape */
    TEST_ASSERT_TRUE(peak <= level * 1.02f);
  }
}

void render_always_replaces_existing_samples(void) {
  dco_init(&dco, 1.0f);
  dco_update(&dco, 1.0f, DCO_TRI, 0, 0, 0.5f, 0.5f);
  dco_note_on(&dco, 440.0f, false);

  float block[AUDIO_BLOCK_SIZE];
  for (size_t i = 0; i < AUDIO_BLOCK_SIZE; i++)
    block[i] = 999.0f;

  dco_render(&dco, block, 0.0f, 0.0f);

  TEST_ASSERT_EQUAL_FLOAT(-0.65f, block[0]); /* prior content fully replaced */
}

/* -------------------------------------------------------------------------
 * Mixer and cross-modulation (ring mod)
 * ---------------------------------------------------------------------- */

/* Two oscillators for the mixer tests: headroom 0.5, saw and tri */
static struct dco mix1, mix2;

static void setup_mix(float level1, float level2) {
  dco_init(&mix1, 0.5f);
  dco_init(&mix2, 0.5f);
  dco_update(&mix1, level1, DCO_SAW, 0, 0, 0.5f, 0.5f);
  dco_update(&mix2, level2, DCO_TRI, 0, 0, 0.5f, 0.5f);
}

/* The ring product's gain at full xmod, as dco_mix() documents it */
static float ring_gain(void) {
  return mix2.mix_level * DCO_RING_GAIN * mix2.gain * dco_unit_scale(&mix1) *
         dco_unit_scale(&mix2);
}

void update_keeps_render_amplitude_independent_of_level(void) {
  setup_mix(1.0f, 1.0f);
  const float full = mix1.level;

  setup_mix(0.25f, 1.0f);

  /* The level setting is applied by dco_mix(), not by the generators */
  TEST_ASSERT_EQUAL_FLOAT(full, mix1.level);
  TEST_ASSERT_EQUAL_FLOAT(0.25f, mix1.mix_level);
}

void mix_at_zero_xmod_is_bit_exact_level_weighted_sum(void) {
  float dst[AUDIO_BLOCK_SIZE];
  float dco1[AUDIO_BLOCK_SIZE];
  float dco2[AUDIO_BLOCK_SIZE];

  for (size_t i = 0; i < AUDIO_BLOCK_SIZE; i++) {
    dco1[i] = 0.3f * sinf(0.11f * (float)i);
    dco2[i] = -0.4f * cosf(0.37f * (float)i);
  }

  setup_mix(0.7f, 0.4f);
  dco_mix(dst, dco1, dco2, &mix1, &mix2, 0.0f);

  for (size_t i = 0; i < AUDIO_BLOCK_SIZE; i++) {
    const float expected = 0.7f * dco1[i] + 0.4f * dco2[i];
    TEST_ASSERT_EQUAL_MEMORY(&expected, &dst[i], sizeof(float));
  }
}

void mix_at_full_xmod_replaces_dco2_with_the_ring(void) {
  float dst[AUDIO_BLOCK_SIZE];
  float dco1[AUDIO_BLOCK_SIZE];
  float dco2[AUDIO_BLOCK_SIZE];

  for (size_t i = 0; i < AUDIO_BLOCK_SIZE; i++) {
    dco1[i] = 0.25f;
    dco2[i] = 0.2f;
  }

  setup_mix(0.5f, 0.8f);
  dco_mix(dst, dco1, dco2, &mix1, &mix2, 1.0f);

  /* Dry DCO1 + ring, no dry DCO2 */
  const float expected = 0.5f * 0.25f + ring_gain() * 0.25f * 0.2f;
  for (size_t i = 0; i < AUDIO_BLOCK_SIZE; i++)
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, expected, dst[i]);
}

void mix_at_full_xmod_with_dco1_muted_is_pure_ring(void) {
  float dst[AUDIO_BLOCK_SIZE];
  float dco1[AUDIO_BLOCK_SIZE];
  float dco2[AUDIO_BLOCK_SIZE];

  for (size_t i = 0; i < AUDIO_BLOCK_SIZE; i++) {
    dco1[i] = 0.25f;
    dco2[i] = 0.2f;
  }

  /* DCO1's level doesn't feed the ring, so muting it leaves the ring whole */
  setup_mix(0.0f, 0.8f);
  dco_mix(dst, dco1, dco2, &mix1, &mix2, 1.0f);

  const float expected = ring_gain() * 0.25f * 0.2f;
  TEST_ASSERT_TRUE(expected > 0.0f);
  for (size_t i = 0; i < AUDIO_BLOCK_SIZE; i++)
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, expected, dst[i]);
}

void mix_crossfades_dco2_channel_by_xmod(void) {
  float dst[AUDIO_BLOCK_SIZE];
  float dco1[AUDIO_BLOCK_SIZE];
  float dco2[AUDIO_BLOCK_SIZE];

  for (size_t i = 0; i < AUDIO_BLOCK_SIZE; i++) {
    dco1[i] = 0.25f;
    dco2[i] = 0.2f;
  }

  setup_mix(0.5f, 0.8f);
  dco_mix(dst, dco1, dco2, &mix1, &mix2, 0.25f);

  const float expected = 0.5f * 0.25f + 0.75f * 0.8f * 0.2f +
                         0.25f * ring_gain() * 0.25f * 0.2f;
  for (size_t i = 0; i < AUDIO_BLOCK_SIZE; i++)
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, expected, dst[i]);
}

/*
 * dco_unit_scale() takes a rendered block back to a +/-1 wave, whatever the
 * waveform and headroom, so the ring product's level is waveform-independent.
 */
static float unit_peak(enum dco_wave wave) {
  float block[AUDIO_BLOCK_SIZE];
  float peak = 0.0f;

  dco_init(&dco, 0.33f);
  dco_update(&dco, 1.0f, wave, 0, 0, 0.5f, 0.5f);
  dco_note_on(&dco, 220.0f, false);

  const float scale = dco_unit_scale(&dco);
  for (int b = 0; b < 8; b++) {
    dco_render(&dco, block, 0.0f, 0.0f);
    for (size_t i = 0; i < AUDIO_BLOCK_SIZE; i++)
      peak = fmaxf(peak, fabsf(block[i] * scale));
  }
  return peak;
}

void unit_scale_normalises_every_waveform_to_unit_peak(void) {
  /* polyBLEP smooths the edges, so saw/pulse peak a little under 1 */
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 1.0f, unit_peak(DCO_TRI));
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 1.0f, unit_peak(DCO_SAW));
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 1.0f, unit_peak(DCO_PULSE));
}

/* -------------------------------------------------------------------------
 * Extreme pitch (C14)
 *
 * The top MIDI note with +2 octaves, +6 semitones, full bend and +1 octave of
 * pitch modulation asks for ~570kHz - a phase increment of ~12 per sample.
 * The generators wrap phase with a single subtraction, so without a ceiling
 * on the increment the phase (and so the output) grows without limit.
 * ---------------------------------------------------------------------- */

static void assert_bounded_at_extreme_pitch(enum dco_wave wave) {
  float out[AUDIO_BLOCK_SIZE];

  dco_init(&dco, 1.0f);
  dco_update(&dco, 1.0f, wave, 2, 6, 0.5f, 0.5f);
  dco_set_bend(&dco, 24.0f);
  dco_note_on(&dco, 12543.85f, false); /* MIDI note 127 */

  for (int b = 0; b < 50; b++) {
    dco_render(&dco, out, 1.0f, 0.0f);

    TEST_ASSERT_TRUE(dco.inc < 0.5f);
    /* The triangle wraps at the start of each sample, so the stored phase
       may be up to one increment over 1 - but never growing beyond that */
    TEST_ASSERT_TRUE(dco.phase >= 0.0f && dco.phase < 1.0f + 0.5f);

    for (uint32_t i = 0; i < AUDIO_BLOCK_SIZE; i++) {
      TEST_ASSERT_TRUE(isfinite(out[i]));
      TEST_ASSERT_TRUE(fabsf(out[i]) <= 3.0f * dco.level);
    }
  }
}

void extreme_pitch_triangle_stays_bounded(void) {
  assert_bounded_at_extreme_pitch(DCO_TRI);
}

void extreme_pitch_saw_stays_bounded(void) {
  assert_bounded_at_extreme_pitch(DCO_SAW);
}

void extreme_pitch_pulse_stays_bounded(void) {
  assert_bounded_at_extreme_pitch(DCO_PULSE);
}

/* Normal pitches are not affected by the ceiling */
void ceiling_does_not_affect_normal_pitch(void) {
  float out[AUDIO_BLOCK_SIZE];

  dco_init(&dco, 1.0f);
  dco_update(&dco, 1.0f, DCO_SAW, 0, 0, 0.5f, 0.5f);
  dco_set_bend(&dco, 0.0f);
  dco_note_on(&dco, 4186.01f, false); /* MIDI note 108, top of a piano */
  dco_render(&dco, out, 0.0f, 0.0f);

  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 4186.01f / AUDIO_SAMPLE_RATE, dco.inc);
}

int main(void) {
  UNITY_BEGIN();

  /* Initialisation / reset */
  RUN_TEST(init_sets_expected_defaults);
  RUN_TEST(reset_sets_phase_for_triangle);
  RUN_TEST(reset_sets_phase_for_non_triangle_waveforms);

  /* Note on / off */
  RUN_TEST(note_on_sets_pitch);
  RUN_TEST(note_on_resets_phase_when_not_legato);
  RUN_TEST(note_on_preserves_phase_when_legato);
  RUN_TEST(note_off_clears_pitch);

  /* Parameter update mapping */
  RUN_TEST(update_scales_level_for_triangle);
  RUN_TEST(update_scales_level_for_saw);
  RUN_TEST(update_scales_level_for_pulse);
  RUN_TEST(update_maps_pulse_width_linearly);
  RUN_TEST(update_calculates_tuning_from_octave_semitones_cents);
  RUN_TEST(fine_tune_centre_is_in_tune);
  RUN_TEST(fine_tune_minimum_is_50_cents_flat);
  RUN_TEST(fine_tune_maximum_is_50_cents_sharp);
  RUN_TEST(fine_tune_encoder_step_is_one_cent);
  RUN_TEST(update_calculates_tuning_with_negative_octave);

  /* PWM */
  RUN_TEST(render_applies_pwm_to_live_pulse_width);
  RUN_TEST(render_clamps_pwm_modulated_pulse_width);

  /* Pitch bend */
  RUN_TEST(set_bend_shifts_phase_increment_by_semitones);
  RUN_TEST(zero_bend_leaves_phase_increment_unchanged);

  /* Render gating & phase increment */
  RUN_TEST(render_is_noop_when_pitch_is_zero);
  RUN_TEST(render_computes_phase_increment_from_pitch);
  RUN_TEST(render_applies_modulation_to_phase_increment);

  /* Waveform generation - closed-form first sample */
  RUN_TEST(triangle_first_sample_matches_closed_form);
  RUN_TEST(saw_first_sample_matches_closed_form);
  RUN_TEST(pulse_first_sample_matches_closed_form_at_symmetric_width);

  /* Waveform generation - frequency tracking */
  RUN_TEST(triangle_frequency_matches_requested_pitch);
  RUN_TEST(saw_frequency_matches_requested_pitch);
  RUN_TEST(pulse_frequency_matches_requested_pitch);

  /* Waveform generation - other invariants */
  RUN_TEST(pulse_dc_offset_stays_near_zero_across_pulse_widths);
  RUN_TEST(amplitude_stays_bounded_across_waveforms);
  RUN_TEST(render_always_replaces_existing_samples);

  /* Cross-modulation (ring mod) */
  RUN_TEST(update_keeps_render_amplitude_independent_of_level);
  RUN_TEST(mix_at_zero_xmod_is_bit_exact_level_weighted_sum);
  RUN_TEST(mix_at_full_xmod_replaces_dco2_with_the_ring);
  RUN_TEST(mix_at_full_xmod_with_dco1_muted_is_pure_ring);
  RUN_TEST(mix_crossfades_dco2_channel_by_xmod);
  RUN_TEST(unit_scale_normalises_every_waveform_to_unit_peak);

  /* Extreme pitch (C14) */
  RUN_TEST(extreme_pitch_triangle_stays_bounded);
  RUN_TEST(extreme_pitch_saw_stays_bounded);
  RUN_TEST(extreme_pitch_pulse_stays_bounded);
  RUN_TEST(ceiling_does_not_affect_normal_pitch);

  return UNITY_END();
}
