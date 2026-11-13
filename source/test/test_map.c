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

#include "map.h"
#include "midi_parser.h"
#include "unity.h"
#include <string.h>

static param_value_t params[PARAM_COUNT];

static enum param_mapping_type set_cc(uint8_t cc, uint8_t value) {
  return param_set_value_from_cc(cc, value, params);
}

void setUp() {
  param_midi_map_init(); /* (re)builds the static CC -> param_id map */
  memset(params, 0, sizeof(params));
}

void tearDown() {}

/* -------------------------------------------------------------------------
 * CC -> parameter mapping
 * ---------------------------------------------------------------------- */

void maps_dca_envelope_ccs_to_uni_float_values(void) {
  TEST_ASSERT_EQUAL(PARAM_VOICE, set_cc(MIDI_CC_VOLUME, 127));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, params[DCA_LEVEL].f);

  TEST_ASSERT_EQUAL(PARAM_VOICE, set_cc(MIDI_CC_ATTACKTIME, 0));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, params[DCA_ATTACK].f);

  TEST_ASSERT_EQUAL(PARAM_VOICE, set_cc(MIDI_CC_DECAYTIME, 64));
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 64.0f / 127.0f, params[DCA_DECAY].f);

  TEST_ASSERT_EQUAL(PARAM_VOICE,
                    set_cc(14, 127)); /* DCA_SUSTAIN has no named MIDI_CC_* */
  TEST_ASSERT_EQUAL_FLOAT(1.0f, params[DCA_SUSTAIN].f);

  TEST_ASSERT_EQUAL(PARAM_VOICE, set_cc(MIDI_CC_RELEASETIME, 0));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, params[DCA_RELEASE].f);
}

void maps_dco1_ccs_across_param_types(void) {
  set_cc(15, 0);
  TEST_ASSERT_EQUAL(DCO_TRI, params[DCO1_WAVE].e);
  set_cc(15, 127);
  TEST_ASSERT_EQUAL(DCO_PULSE, params[DCO1_WAVE].e);

  set_cc(17, 0);
  TEST_ASSERT_EQUAL(-OSC_OCTAVE_RANGE, params[DCO1_OCTAVE].i);
  set_cc(17, 64);
  TEST_ASSERT_EQUAL(0, params[DCO1_OCTAVE].i);
  set_cc(17, 127);
  TEST_ASSERT_EQUAL(OSC_OCTAVE_RANGE, params[DCO1_OCTAVE].i);

  set_cc(21, 0);
  TEST_ASSERT_EQUAL(LFO1_TRI, params[DCO1_MOD_SOURCE].e);
  set_cc(21, 127);
  TEST_ASSERT_EQUAL(ENV1_LEVEL,
                    params[DCO1_MOD_SOURCE].e); /* last mod_source enum value */

  set_cc(16, 127);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, params[DCO1_LEVEL].f);
}

void maps_dco2_ccs_to_expected_params(void) {
  set_cc(23, 127);
  TEST_ASSERT_EQUAL(DCO_PULSE, params[DCO2_WAVE].e);

  set_cc(25, 0);
  TEST_ASSERT_EQUAL(-OSC_OCTAVE_RANGE, params[DCO2_OCTAVE].i);

  set_cc(29, 127);
  TEST_ASSERT_EQUAL(ENV1_LEVEL,
                    params[DCO2_MOD_SOURCE].e); /* last mod_source enum value */

  set_cc(105, 127);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, params[DCO2_PWM_DEPTH].f);
}

void maps_filter_ccs_to_expected_params(void) {
  set_cc(58, 127);
  TEST_ASSERT_EQUAL(FILTER_HPF4, params[DCF_MODE].e);

  TEST_ASSERT_EQUAL(PARAM_VOICE, set_cc(MIDI_CC_FREQUENCYCUTOFF, 127));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, params[DCF_CUTOFF].f);

  TEST_ASSERT_EQUAL(PARAM_VOICE, set_cc(MIDI_CC_RESONANCE, 0));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, params[DCF_RES].f);
}

void bool_param_threshold_is_64(void) {
  set_cc(60, 63);
  TEST_ASSERT_FALSE(params[DCF_TRACK].b);

  set_cc(60, 64);
  TEST_ASSERT_TRUE(params[DCF_TRACK].b);
}

void maps_env1_and_lfo_ccs_to_expected_params(void) {
  set_cc(31, 127);
  TEST_ASSERT_EQUAL(ENV_INVERTED_BIASED, params[ENV1_MODE].e);

  set_cc(57, 0);
  TEST_ASSERT_EQUAL(LFO_MODE_FREE, params[LFO1_MODE].e);
  set_cc(57, 127);
  TEST_ASSERT_EQUAL(LFO_MODE_NOTE, params[LFO1_MODE].e);

  set_cc(56, 127);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, params[LFO1_RATE].f);
}

void maps_global_ccs_return_synth_mapping_type(void) {
  TEST_ASSERT_EQUAL(PARAM_SYNTH, set_cc(116, 127)); /* voice mode */
  TEST_ASSERT_EQUAL(VOICE_MODE_MONO, params[SYNTH_VOICE_MODE].e);

  TEST_ASSERT_EQUAL(PARAM_SYNTH, set_cc(MIDI_CC_PORTAMENTO, 100));
  TEST_ASSERT_TRUE(params[SYNTH_PORTAMENTO_ON].b);

  TEST_ASSERT_EQUAL(PARAM_SYNTH, set_cc(MIDI_CC_PORTAMENTOTIME, 127));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, params[SYNTH_PORTAMENTO_TIME].f);

  TEST_ASSERT_EQUAL(PARAM_SYNTH, set_cc(MIDI_CC_DETUNELEVEL, 0));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, params[SYNTH_UNISON_DETUNE].f);

  TEST_ASSERT_EQUAL(PARAM_SYNTH, set_cc(106, 127));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, params[SYNTH_BEND_RANGE].f);

  TEST_ASSERT_EQUAL(PARAM_SYNTH, set_cc(108, 127));
  TEST_ASSERT_EQUAL(
      MOD_WHEEL_DEST_VCF,
      params[SYNTH_MOD_WHEEL_DEST].e); /* last mod_wheel_dest enum value */
}

void unmapped_cc_returns_unmapped_and_leaves_params_untouched(void) {
  memset(params, 0x7F, sizeof(params)); /* distinctive non-zero sentinel */
  param_value_t before[PARAM_COUNT];
  memcpy(before, params, sizeof(params));

  enum param_mapping_type mapping =
      param_set_value_from_cc(MIDI_CC_MODULATIONWHEEL, 100, params);

  TEST_ASSERT_EQUAL(PARAM_UNMAPPED, mapping);
  TEST_ASSERT_EQUAL_MEMORY(before, params, sizeof(params));
}

/* -------------------------------------------------------------------------
 * Map coverage (M1) and reserved CCs (C8)
 * ---------------------------------------------------------------------- */

/* M1: every patch parameter can be set from MIDI - exactly one CC each */
void every_parameter_has_exactly_one_cc(void) {
  int cc_count[PARAM_COUNT] = {0};

  for (int cc = 0; cc < 128; cc++) {
    enum param_id id;
    if (param_midi_map_lookup((uint8_t)cc, &id)) {
      TEST_ASSERT_TRUE(id < PARAM_COUNT);
      cc_count[id]++;
    }
  }

  for (int id = 0; id < PARAM_COUNT; id++) {
    if (id == VOICE_PARAM_COUNT) {
      continue; /* section marker, not a parameter */
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, cc_count[id],
                                  "parameter without exactly one CC");
  }
}

/*
 * C8: CCs the dispatcher handles itself must not also be mapped to a
 * parameter - the dispatcher sees them first, so the parameter would be
 * unreachable (as Chorus On was on CC109).
 */
void dispatcher_ccs_are_not_mapped_to_parameters(void) {
  const uint8_t reserved[] = {
      MIDI_CC_MODULATIONWHEEL, MIDI_CC_HOLDPEDAL,     MIDI_CC_SOSTENUTO,
      MIDI_CC_SOFT_TAKEOVER,   MIDI_CC_ALLSOUNDOFF,   MIDI_CC_RESETALLCONTROLLERS,
      MIDI_CC_LOCALCONTROL,    MIDI_CC_ALLNOTESOFF,   MIDI_CC_OMNIMODEOFF,
      MIDI_CC_OMNIMODEON,      MIDI_CC_MONOMODEON,    MIDI_CC_POLYMODEON};

  for (size_t i = 0; i < sizeof(reserved); i++) {
    TEST_ASSERT_FALSE(param_midi_map_lookup(reserved[i], NULL));
  }
}

void lookup_reports_unmapped_and_out_of_range_ccs(void) {
  enum param_id id = DCA_LEVEL;
  TEST_ASSERT_FALSE(param_midi_map_lookup(MIDI_CC_BANKSELECT, &id));
  TEST_ASSERT_FALSE(param_midi_map_lookup(200, &id));
  TEST_ASSERT_EQUAL(DCA_LEVEL, id); /* untouched on failure */
}

/* C8: chorus is on the standard Chorus Level controller, CC93 */
void chorus_on_is_mapped_to_cc93(void) {
  TEST_ASSERT_EQUAL(PARAM_SYNTH, set_cc(MIDI_CC_CHORUSLEVEL, 127));
  TEST_ASSERT_TRUE(params[SYNTH_CHORUS_ON].b);

  TEST_ASSERT_EQUAL(PARAM_SYNTH, set_cc(MIDI_CC_CHORUSLEVEL, 0));
  TEST_ASSERT_FALSE(params[SYNTH_CHORUS_ON].b);
}

/* M1: the six previously unmapped parameters */
void maps_dca_and_dcf_modulation_ccs(void) {
  TEST_ASSERT_EQUAL(PARAM_VOICE, set_cc(110, 127));
  TEST_ASSERT_EQUAL(ENV1_LEVEL, params[DCA_MOD_SOURCE].e); /* last source */

  TEST_ASSERT_EQUAL(PARAM_VOICE, set_cc(111, 127));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, params[DCA_MOD_DEPTH].f);

  TEST_ASSERT_EQUAL(PARAM_VOICE, set_cc(112, 127));
  TEST_ASSERT_TRUE(params[DCA_NOTE_TRACK].b);

  TEST_ASSERT_EQUAL(PARAM_VOICE, set_cc(113, 127));
  TEST_ASSERT_TRUE(params[DCA_VELOCITY_TRACK].b);

  TEST_ASSERT_EQUAL(PARAM_VOICE, set_cc(114, 0));
  TEST_ASSERT_EQUAL(LFO1_TRI, params[DCF_MOD_SOURCE].e); /* first source */

  TEST_ASSERT_EQUAL(PARAM_VOICE, set_cc(115, 127));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, params[DCF_MOD_DEPTH].f);
}

int main(void) {
  UNITY_BEGIN();

  /* CC -> parameter mapping */
  RUN_TEST(maps_dca_envelope_ccs_to_uni_float_values);
  RUN_TEST(maps_dco1_ccs_across_param_types);
  RUN_TEST(maps_dco2_ccs_to_expected_params);
  RUN_TEST(maps_filter_ccs_to_expected_params);
  RUN_TEST(bool_param_threshold_is_64);
  RUN_TEST(maps_env1_and_lfo_ccs_to_expected_params);
  RUN_TEST(maps_global_ccs_return_synth_mapping_type);
  RUN_TEST(unmapped_cc_returns_unmapped_and_leaves_params_untouched);

  /* Map coverage (M1) and reserved CCs (C8) */
  RUN_TEST(every_parameter_has_exactly_one_cc);
  RUN_TEST(dispatcher_ccs_are_not_mapped_to_parameters);
  RUN_TEST(lookup_reports_unmapped_and_out_of_range_ccs);
  RUN_TEST(chorus_on_is_mapped_to_cc93);
  RUN_TEST(maps_dca_and_dcf_modulation_ccs);

  return UNITY_END();
}
