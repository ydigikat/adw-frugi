/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   The DCO implements the audio rate oscillators.  These are band-limited where
   advantaegous (anti-alised).  The DCO is the typical classic substractive trio
   (TRI, SAW, PULSE).

   Like many other modules, a jump-table is used to avoid conditional logic in
   sample/block loops.  A function pointer to each generator is placed in the
   table which can then be indexed directly to call the function indexed on the
   waveform type.

   The DSP code is typical, well covered by many academic texts.
   -----------------------------------------------------------------------------
*/

#include "dco.h"
#include "param.h"

/*
 * Level perception scalers, different waveforms are perceived by us
 * as having different loudness due to harmonic content. The scalars
 * alter the individual levels slightly to compensate for this.
 *
 * This is a subjective value, set by ear, and is the reason my summing
 * gain-staging sometimes produces overshoots, for which I employ a
 * clipper in the final output.
 */
static const float wave_gain[DCO_WAVE_COUNT] = {
    [DCO_TRI] = 0.65f,
    [DCO_SAW] = 0.60f,
    [DCO_PULSE] = 0.45f,
};

/*
 * Pulse width is clamped to avoid messing with BLEP window.
 */
#define DCO_PULSEWIDTH_MIN (0.05f)
#define DCO_PULSEWIDTH_MAX (0.95f)

/*
 * Ceiling on the phase increment (cycles per sample), just below Nyquist
 * (0.5).  See dco_render() for why it is needed.
 */
#define DCO_INC_MAX (0.49f)

static void generate_tri(struct dco *dco, float sample_block[]);
static void generate_saw(struct dco *dco, float sample_block[]);
static void generate_pulse(struct dco *dco, float sample_block[]);

static void (*sound_generators[])(struct dco *dco, float sample_block[]) = {
    generate_tri, generate_saw, generate_pulse};

/*
 * Initialise the oscillator
 */
void dco_init(struct dco *dco, float gain) {
  RTT_ASSERT(dco);

  dco->gain = gain;
  dco->phase = 0.0f;
  dco->inc = 0.0f;
  dco->pitch = 0;
  dco_reset(dco);
}

/*
 * Reset the oscillator - triangle wave starts 180 degrees out of phase.
 */
void dco_reset(struct dco *dco) {
  RTT_ASSERT(dco);

  dco->phase = (dco->waveform == DCO_TRI) ? 0.5f : 0;
}

/*
 * Renders a note - calculate phase increment and fill block.
 */
void dco_render(struct dco *dco, float sample_block[], float pitch_mod,
                float pwm_mod) {
  RTT_ASSERT(dco);
  RTT_ASSERT(sample_block);

#ifdef TRACE_DCO
  DWT_INIT()
  DWT_CLEAR();
#endif

  if (dco->pitch == 0) {
    return;
  }

  /* Calculate the phase increment */
  float bend = dco->pitch_bend_semitones / 12.0f;
  dco->inc = (powf(2.0f, dco->tuning + bend + pitch_mod) * dco->pitch) /
             AUDIO_SAMPLE_RATE;

  /*
   * Clamp the increment below Nyquist.  Octave, semitone, bend and pitch
   * modulation all multiply the note frequency, so a high note can ask for
   * far more than the sample rate - MIDI note 127 with +2 octaves, +6
   * semitones, full bend and +1 octave of modulation is ~570kHz, an
   * increment of ~12 per sample.
   *
   * The generators wrap the phase with a single subtraction, so with an
   * increment above 1 the phase is never brought back into 0..1 and grows
   * without limit, and so does the output (millions within a fraction of a
   * second - full-scale noise into the mixer).  The polyBLEP corrections
   * also assume the increment is below 0.5.  Anything that high is above
   * hearing anyway, so clamping costs nothing musically.
   */
  dco->inc = fminf(dco->inc, DCO_INC_MAX);

  /* PWM - only for the pulse wave */
  dco->pulse_width =
      fmaxf(DCO_PULSEWIDTH_MIN,
            fminf(DCO_PULSEWIDTH_MAX, dco->pulse_width_base + pwm_mod));

#ifdef TRACE_LEVELS
  // SEGGER_RTT_WriteString(0,RTT_CTRL_TEXT_BRIGHT_GREEN);
  // RTT_LOG_FLOAT("dco_level:",dco->level);
#endif

  /* Call the sound generator directly based on the wave type parameter */
  sound_generators[dco->waveform](dco, sample_block);

#ifdef TRACE_LEVELS
  float peak = 0.0f;
  for (uint32_t i = 0; i < AUDIO_BLOCK_SIZE; i++)
    peak = fmaxf(peak, fabsf(sample_block[i]));

  if (peak > 1.0f) {
    RTT_BLUE();
    RTT_LOG_FLOAT("DCO overshoot:", peak);
    RTT_WHITE();
  }
#endif

#ifdef TRACE_DCO
  DWT_OUTPUT("dco_render:");
#endif
}

/*
 * The DCO mixer.  Combines DCO1 and DCO2's independently-rendered blocks
 * into dst_block, applying each DCO's level setting and the ring modulator.
 *
 * The ring modulator sits in DCO2's mixer slot (as on e.g. the MS-20 or
 * minilogue): `xmod` (0..1) crossfades DCO2's channel from DCO2 itself to
 * the ring product DCO1 x DCO2, while DCO1 stays in the mix untouched:
 *
 *   ring   = dco1n * dco2n * dco2->gain * DCO_RING_GAIN
 *   dst[i] = l1 * dco1[i] + l2 * ((1 - xmod) * dco2[i] + xmod * ring)
 *
 * where l1/l2 are the DCO level settings and dco1n/dco2n are the rendered
 * waves normalised to +/-1 (dco_unit_scale()), so the ring's strength
 * doesn't depend on the waveforms chosen.
 *
 * So at full xmod:
 *  - DCO2's level is the ring level;
 *  - DCO1's level is only the dry DCO1 level (it doesn't feed the ring), so
 *    DCO1 at 0 gives pure ring mod - both input pitches gone, only their sum
 *    and difference frequencies left - and DCO1 up adds back the fundamental.
 *
 * xmod = 0 is a bit-exact bypass (plain l1*dco1 + l2*dco2).
 */
void dco_mix(float dst_block[], const float dco1_block[],
             const float dco2_block[], const struct dco *dco1,
             const struct dco *dco2, float xmod) {
  RTT_ASSERT(dst_block);
  RTT_ASSERT(dco1_block);
  RTT_ASSERT(dco2_block);
  RTT_ASSERT(dco1);
  RTT_ASSERT(dco2);

  float *restrict out = dst_block;
  const float *restrict d1 = dco1_block;
  const float *restrict d2 = dco2_block;
  const float *restrict end = out + AUDIO_BLOCK_SIZE;

  /* Block-constant gains, so the loop is three multiply-adds */
  const float g1 = dco1->mix_level;
  const float g2 = (1.0f - xmod) * dco2->mix_level;
  const float g_ring = xmod * dco2->mix_level * DCO_RING_GAIN * dco2->gain *
                       dco_unit_scale(dco1) * dco_unit_scale(dco2);

#ifdef TRACE_DCO
  RTT_LOG_FLOAT("x-mod", xmod);
  RTT_LOG_FLOAT("ring gain", g_ring);
#endif

#pragma clang loop unroll_count(4)

  while (out < end) {
    *out = g1 * (*d1) + g2 * (*d2) + g_ring * (*d1) * (*d2);
    out++;
    d1++;
    d2++;
  }
}

/*
 * The factor that takes this oscillator's rendered output back to a +/-1
 * waveform, i.e. removes the fixed per-voice headroom (dco->gain) and the
 * per-waveform loudness scaler (wave_gain[]).  Used to normalise the
 * ring-mod product so its strength doesn't depend on the waveforms chosen.
 */
float dco_unit_scale(const struct dco *dco) {
  RTT_ASSERT(dco);
  return 1.0f / (dco->gain * wave_gain[dco->waveform]);
}

/*
 * Note on request, pitch is calculated by the voice prior to call.
 */
void dco_note_on(struct dco *dco, float pitch, bool is_legato) {
  RTT_ASSERT(dco);
  if (!is_legato) {
    dco_reset(dco);
  }

  dco->pitch = pitch;
}

/*
 * Note off request.  We can only play one note so no need
 * to specify.
 */
void dco_note_off(struct dco *dco) {
  RTT_ASSERT(dco);
  dco->pitch = 0;
}

/*
 * Sets the live pitch bend offset (semitones), applied continuously in
 * dco_render() without retriggering the note or resetting phase.
 */
void dco_set_bend(struct dco *dco, float pitch_bend_semitones) {
  RTT_ASSERT(dco);
  dco->pitch_bend_semitones = pitch_bend_semitones;
}

/*
 * Explicitly sets the oscillator's phase (0..1), overriding the waveform's
 * default reset point. Used to stagger unison voices - see
 * voice_start_chain().
 */
void dco_set_phase(struct dco *dco, float phase) {
  RTT_ASSERT(dco);
  dco->phase = phase - floorf(phase);
}

/*
 * Refresh internal state from the params.
 */
void dco_update(struct dco *dco, float level, enum dco_wave waveform,
                int32_t octave, int32_t semitones, float fine_tune,
                float pulse_width) {
  dco->waveform = waveform;
  dco->pulse_width_base =
      param_uni_to_linear(pulse_width, DCO_PULSEWIDTH_MIN, DCO_PULSEWIDTH_MAX);

  /*
   * The generators render at the oscillator's fixed amplitude (headroom x
   * waveform loudness); the level setting is applied later in dco_mix(), so
   * that DCO1's level doesn't also scale its contribution to the ring mod.
   */
  dco->level = dco->gain * wave_gain[dco->waveform];
  dco->mix_level = level;

  /*
   * Pitch tuning in octaves.  fine_tune is the normalised 0..1 parameter,
   * mapped to +/-OSC_CENTS_RANGE (50) cents either side of the note, so 0.5
   * is in tune and one UI encoder step (0.01) is one cent.
   */
  const float cents =
      param_uni_to_linear(fine_tune, -OSC_CENTS_RANGE, OSC_CENTS_RANGE);
  dco->tuning = ((float)(octave * 12 + semitones) + cents / 100.0f) / 12.0f;

#ifdef TRACE_DCO
  RTT_LOG_FLOAT("DCO: cents:", cents);
  RTT_LOG_FLOAT("DCO: tuning:", dco->tuning);
#endif
}

/*
 * Generate triangle wave.
 *
 * Not anti-aliased this as any aliasing is musically inaudible.
 */
static void generate_tri(struct dco *dco, float *sample_block) {
  float *restrict ptr = sample_block;
  const float *restrict end = ptr + AUDIO_BLOCK_SIZE;

  /* Stack caching of variables used in sample generation loop */
  float phase = dco->phase;
  const float inc = dco->inc;
  const float level = dco->level;

  #pragma clang loop unroll_count(4)

  while (ptr < end) {
    if (phase >= 1.0f)
      phase -= 1.0f;

    float sample = (2.0f * fabsf(2.0f * phase - 1.0f) - 1.0f) * level;

    *ptr++ = sample;

    phase += inc;
  }
  dco->phase = phase;
}

/*
 * Generates a saw tooth wave
 * Polynomial BLEP anti-aliasing.
 */
static void generate_saw(struct dco *dco, float *sample_block) {
  float *restrict ptr = sample_block;
  const float *restrict end = ptr + AUDIO_BLOCK_SIZE;

  /* Stack caching of variables used in sample generation loop */
  float phase = dco->phase;
  const float inc = dco->inc;
  const float level = dco->level;

  #pragma clang loop unroll_count(4)

  while (ptr < end) {
    float saw = 2.0f * phase - 1.0f;

    /* Falling edge polynomial BLEP */
    if (phase > 1.0f - inc) {
      float t = (phase - 1.0f) / inc;
      saw += (t * t + 2.0f * t + 1.0f) * -1.0f;
    } else if (phase < inc) {
      /* RH side */
      float t = phase / inc;
      saw += (2.0f * t - t * t - 1.0f) * -1.0f;
    }

    phase += inc;

    if (phase > 1.0f)
      phase -= 1.0f;

    float sample = saw * level;

    *ptr++ = sample;
  }

  dco->phase = phase;
}

/*
 * Generates pulse wave with polyBLEP band-limiting.
 */
static void generate_pulse(struct dco *dco, float *sample_block) {
  float *restrict ptr = sample_block;
  const float *restrict end = ptr + AUDIO_BLOCK_SIZE;

  /* Stack caching of variables used in sample generation loop */
  float phase = dco->phase;
  const float inc = dco->inc;
  const float level = dco->level;
  const float pulse_width = dco->pulse_width;

  /* Block-constant DC offset removal */
  const float dc_offset = 2.0f * pulse_width - 1.0f;
  const float corr = 1.0f / (1.0f + fabsf(dc_offset));

  #pragma clang loop unroll_count(4)

  while (ptr < end) {
    /* Defensive wrap */
    if (phase >= 1.0f)
      phase -= 1.0f;

    /* Naive (aliased) pulse: +1 for the first pulse_width of the cycle, -1 for
     * the remainder. */
    float pulse = (phase < pulse_width) ? 1.0f : -1.0f;

    /* --- Edge 1: phase wrap (0.0/1.0), rising -1 -> +1 */
    if (phase > 1.0f - inc) {
      float t = (phase - 1.0f) / inc;
      pulse += (t * t + 2.0f * t + 1.0f);
    } else if (phase < inc) {
      float t = phase / inc;
      pulse += (2.0f * t - t * t - 1.0f);
    }

    /* Edge 2: duty-cycle crossing (pulse_width), falling +1 -> -1 */
    float d = phase - pulse_width;

    if (d > -inc && d <= 0.0f) {
      float t = d / inc;
      pulse += (t * t + 2.0f * t + 1.0f) * -1.0f;
    } else if (d > 0.0f && d < inc) {
      float t = d / inc;
      pulse += (2.0f * t - t * t - 1.0f) * -1.0f;
    }

    phase += inc;

    if (phase > 1.0f)
      phase -= 1.0f;

    float sample = (pulse - dc_offset) * corr * level;

    *ptr++ = sample;
  }

  dco->phase = phase;
}
