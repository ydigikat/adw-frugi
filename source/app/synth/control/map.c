/* -----------------------------------------------------------------------------
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   This module handles:

   1. The mapping of CC (MIDI continous controller) values (the knob position)
      to the internal Frugi parameter value. For mappings, it creates a sparse
      array which holds the CC->ID mapping, covering all possible CC message
      numbers but leaving most unmapped (MIDI_CC_UNSUPPORTED).

      We end up with an array which has a 1:1 mapping if CC to parameter ID.

  2.  Setting/getting the parameter value by CC.  The set convers the CC data
      value to the correct representation for the parameter it
      represents - (float, int, enum or bool)

   3. Soft takeover mode, in this mode the MIDI CC is ignored until the value
      already set on the control is matched or crossed, this stops the parameter
      jumping around if the controller knob starts out misaligned with it.
   -----------------------------------------------------------------------------
*/
#include "map.h"
#include "midi_parser.h"

/*
 * A CC value outside the 7-bit range, marks a control as not yet seen.
 */
#define TAKEOVER_CC_UNSEEN (0xFFu)

/* CC -> ID mapping array */
static uint8_t param_map[128];

/*
 * Soft takeover state. By CC number (not param_id) since that's what
 * arrives with each message.
 */
static bool soft_takeover_enabled = false;
static uint8_t takeover_last_cc[128];

void param_midi_map_init(void) {
  /*
   * Set all to unassigned creating a sparse array to be filled out with any
   * mappings.  By default all CC->ID slots are unmapped.
   */
  for (int i = 0; i < 128; i++) {
    param_map[i] = MIDI_CC_UNSUPPORTED;
    takeover_last_cc[i] = TAKEOVER_CC_UNSEEN;
  }

  /*
   * Map CCs to param ids, the index is the CC number and value the
   * parameter_id. Any unmapped have MIDI_CC_UNSUPPORTED.
   *
   * When we get a CC message we lookup the parameter directly.
   */

  param_map[MIDI_CC_VOLUME] = DCA_LEVEL;
  param_map[110] = DCA_MOD_SOURCE;
  param_map[111] = DCA_MOD_DEPTH;
  param_map[112] = DCA_NOTE_TRACK;
  param_map[113] = DCA_VELOCITY_TRACK;

  param_map[MIDI_CC_ATTACKTIME] = DCA_ATTACK;
  param_map[MIDI_CC_DECAYTIME] = DCA_DECAY;
  param_map[14] = DCA_SUSTAIN;
  param_map[MIDI_CC_RELEASETIME] = DCA_RELEASE;

  param_map[15] = DCO1_WAVE;
  param_map[16] = DCO1_LEVEL;
  param_map[17] = DCO1_OCTAVE;
  param_map[18] = DCO1_SEMI;
  param_map[19] = DCO1_CENTS;
  param_map[20] = DCO1_PULSEWIDTH;
  param_map[21] = DCO1_MOD_SOURCE;
  param_map[22] = DCO1_MOD_DEPTH;
  param_map[102] = DCO1_PWM_SOURCE;
  param_map[103] = DCO1_PWM_DEPTH;

  param_map[23] = DCO2_WAVE;
  param_map[24] = DCO2_LEVEL;
  param_map[25] = DCO2_OCTAVE;
  param_map[26] = DCO2_SEMI;
  param_map[27] = DCO2_CENTS;
  param_map[28] = DCO2_PULSEWIDTH;
  param_map[29] = DCO2_MOD_SOURCE;
  param_map[30] = DCO2_MOD_DEPTH;
  param_map[104] = DCO2_PWM_SOURCE;
  param_map[105] = DCO2_PWM_DEPTH;

  param_map[107] = NOISE_LEVEL;
  param_map[61] = DCO_DRIVE;
  param_map[62] = DCO_RINGMOD_LEVEL;

  param_map[31] = ENV1_MODE;
  param_map[52] = ENV1_ATTACK;
  param_map[53] = ENV1_DECAY;
  param_map[54] = ENV1_SUSTAIN;
  param_map[55] = ENV1_RELEASE;

  param_map[56] = LFO1_RATE;
  param_map[57] = LFO1_MODE;

  param_map[58] = DCF_MODE;
  param_map[59] = DCF_SAT;
  param_map[MIDI_CC_FREQUENCYCUTOFF] = DCF_CUTOFF;
  param_map[MIDI_CC_RESONANCE] = DCF_RES;
  param_map[60] = DCF_TRACK;
  param_map[114] = DCF_MOD_SOURCE;
  param_map[115] = DCF_MOD_DEPTH;

  param_map[116] = SYNTH_VOICE_MODE;
  param_map[MIDI_CC_PORTAMENTO] = SYNTH_PORTAMENTO_ON;
  param_map[MIDI_CC_PORTAMENTOTIME] = SYNTH_PORTAMENTO_TIME;
  param_map[MIDI_CC_DETUNELEVEL] = SYNTH_UNISON_DETUNE;
  param_map[106] = SYNTH_BEND_RANGE;
  param_map[108] = SYNTH_MOD_WHEEL_DEST;
  param_map[MIDI_CC_CHORUSLEVEL] = SYNTH_CHORUS_ON; /* CC93, on at >= 64 */
}

/*
 * Reverse lookup for a single CC - used by tests mainly. 
 * Returns false if the CC is not mapped.
 */
bool param_midi_map_lookup(uint8_t cc, enum param_id *id) {
  if (cc >= 128 || param_map[cc] == MIDI_CC_UNSUPPORTED) {
    return false;
  }

  if (id) {
    *id = (enum param_id)param_map[cc];
  }

  return true;
}

/*
 * The inverse of the switch in param_set_value_from_cc() below: given a
 * param's current value, find the CC value that would produce it. Soft
 * takeover calls this.
 */
static uint8_t param_value_to_cc7bit(uint8_t param_id,
                                     const param_value_t params[]) {
  const struct param_desc *desc = &param_desc[param_id];

  switch (desc->type) {
  case PARAM_TYPE_ENUM:
    return param_enum_to_cc7bit(params[param_id].e, desc->enum_count);

  case PARAM_TYPE_BOOL:
    return param_bool_to_cc7bit(params[param_id].b);

  case PARAM_TYPE_INT:
    return param_int_to_cc7bit(params[param_id].i, desc->min, desc->max);

  case PARAM_TYPE_FLOAT:
  default:
    return param_uni_to_cc7bit(params[param_id].f);
  }
}

void param_set_soft_takeover(bool enabled) {
  soft_takeover_enabled = enabled;

  /*
   * Re-arm tracking on every toggle so a stale reading from before the
   * switch can't look like an immediate crossing and let a knob jump a
   * parameter the moment takeover is (re-)enabled.
   */
  for (int i = 0; i < 128; i++) {
    takeover_last_cc[i] = TAKEOVER_CC_UNSEEN;
  }
}

/*
 * Set parameter
 *
 * We find the parameter to which this CC is mapped (if any) and from that
 * the param_id to set the value in the param array:
 *
 * 1. Check for the CC being in the param_map array and not MIDI_CC_UNSUPPORTED.
 * 2. Use the value in the CC array to index the param array and store the value
 *    at that array position.  The value in the CC array is the param_id value
 *    which is also the index of its value in the params array.
 *
 * With soft takeover enabled the incoming value is only accepted once it
 * matches or crosses the parameter's current value,so a hardware control
 * that's out of sync can't jerk the parameter to wherever it happens to be
 * set.
 */
enum param_mapping_type param_set_value_from_cc(uint8_t cc, uint8_t value,
                                                param_value_t params[]) {
  uint8_t param_id = param_map[cc];

  if (param_id == MIDI_CC_UNSUPPORTED) {
    return PARAM_UNMAPPED;
  }

  if (soft_takeover_enabled) {
    uint8_t target = param_value_to_cc7bit(param_id, params);
    uint8_t last = takeover_last_cc[cc];

    bool crossed = (last == TAKEOVER_CC_UNSEEN)
                       ? (value == target)
                       : ((last <= target && value >= target) ||
                          (last >= target && value <= target));

    takeover_last_cc[cc] = value;

    if (!crossed) {
      return PARAM_PENDING;
    }
  }

  const struct param_desc *desc = &param_desc[param_id];

  switch (desc->type) {
  case PARAM_TYPE_ENUM:
    params[param_id].e = param_cc7bit_to_enum(value, desc->enum_count);
    break;

  case PARAM_TYPE_BOOL:
    params[param_id].b = param_cc7bit_to_bool(value);
    break;

  case PARAM_TYPE_INT:
    params[param_id].i = param_cc7bit_to_int(value, desc->min, desc->max);
    break;

  case PARAM_TYPE_FLOAT:
  default:
    params[param_id].f = param_cc7bit_to_uni(value);
    break;
  }

  return param_id < VOICE_PARAM_COUNT ? PARAM_VOICE : PARAM_SYNTH;
}
