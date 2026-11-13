/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   The default/"init" patch - see patch.h. Every other patch is user
   defined and lives in EEPROM (patch_store.h/patch_persist.h); this is
   the one hard-coded fallback.

   Every field of params[] is set explicitly, even ones whose "off" value
   happens to be the zero value - callers may pass a stack buffer rather
   than zero-initialised storage, so nothing here can rely on that.
   -----------------------------------------------------------------------------
*/
#include "patch.h"

void param_create_default_patch(param_value_t params[]) {
  /* Polyphonc */
  params[SYNTH_VOICE_MODE].e = VOICE_MODE_POLY;

  /* Unity gain */
  params[DCA_LEVEL].f = 1.0f;

  /* DCA Envelope is gated*/
  params[DCA_ATTACK].f = 0;
  params[DCA_DECAY].f = 0;
  params[DCA_SUSTAIN].f = 1.0f;
  params[DCA_RELEASE].f = 0;
  params[DCA_MOD_SOURCE].e = LFO1_SAW;
  params[DCA_MOD_DEPTH].f = 0;
  params[DCA_NOTE_TRACK].b = false;
  params[DCA_VELOCITY_TRACK].b = false;

  /* Saw wave at unity gain */
  params[DCO1_WAVE].e = DCO_SAW;
  params[DCO1_LEVEL].f = 1.0f;
  params[DCO1_OCTAVE].i = 0;
  params[DCO1_SEMI].i = 0;
  params[DCO1_CENTS].f = 0.5f; /* in tune (fine tune is +/-50 cents) */
  params[DCO1_PULSEWIDTH].f = 0.5f;
  params[DCO1_MOD_SOURCE].e = LFO1_SAW;
  params[DCO1_MOD_DEPTH].f = 0;
  params[DCO1_PWM_SOURCE].e = LFO1_TRI;
  params[DCO1_PWM_DEPTH].f = 0;

  /* Saw wave at unity gain */
  params[DCO2_WAVE].e = DCO_TRI;
  params[DCO2_LEVEL].f = 1.0f;
  params[DCO2_OCTAVE].i = 0;
  params[DCO2_SEMI].i = 0;
  params[DCO2_CENTS].f = 0.5f; /* in tune (fine tune is +/-50 cents) */
  params[DCO2_PULSEWIDTH].f = 0.5f;
  params[DCO2_MOD_SOURCE].e = LFO1_RAMP;
  params[DCO2_MOD_DEPTH].f = 0;
  params[DCO2_PWM_SOURCE].e = LFO1_TRI;
  params[DCO2_PWM_DEPTH].f = 0;

  /* Noise off */
  params[NOISE_LEVEL].f = 0;

  /* Waveshaper off (k=0 is exact bypass, see waveshaper.c) */
  params[DCO_DRIVE].f = 0;

  /* Cross-mod (ring mod) off - 0 is a bit-exact bypass, see dco_mix() */
  params[DCO_RINGMOD_LEVEL].f = 0;

  /* LPF2, fully open, no res, no drive, no tracking*/
  params[DCF_MODE].e = FILTER_LPF2;
  params[DCF_CUTOFF].f = 1.0f;
  params[DCF_RES].f = 0;
  params[DCF_SAT].f = 0;
  params[DCF_TRACK].b = false;
  params[DCF_MOD_SOURCE].e = LFO1_TRI;
  params[DCF_MOD_DEPTH].f = 0;

  /* LFO OFF */
  params[LFO1_RATE].f = 0.5f;
  params[LFO1_MODE].e = LFO_MODE_NOTE;

  /* Filter (mod) env gated, normal mode */
  params[ENV1_ATTACK].f = 0;
  params[ENV1_DECAY].f = 0;
  params[ENV1_SUSTAIN].f = 1.0f;
  params[ENV1_RELEASE].f = 0.5f;
  params[ENV1_MODE].e = ENV_NORMAL;

  /* Performance controls off */
  params[SYNTH_PORTAMENTO_ON].b = false;
  params[SYNTH_PORTAMENTO_TIME].f = 0;
  params[SYNTH_UNISON_DETUNE].f = 0;
  params[SYNTH_BEND_RANGE].f =
      2.0f / BEND_RANGE_MAX; /* +/- 2 semitones, the MIDI default */
  params[SYNTH_MOD_WHEEL_DEST].e = MOD_WHEEL_DEST_VCO;
  params[SYNTH_CHORUS_ON].b = false;
}
