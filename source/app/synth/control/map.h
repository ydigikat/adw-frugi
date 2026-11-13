/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
*/
#ifndef FRUGI_PARAM_MIDI_MAP_H
#define FRUGI_PARAM_MIDI_MAP_H

#include <stdbool.h>
#include <stdint.h>

#include "param.h"

/*
 * Maps incoming MIDI CC numbers onto param_id values and decodes the
 * 7-bit CC data byte into a parameter's native representation. Knows
 * nothing about patch storage or factory content.
 *
 * The PARAM_PENDING indicates mapped but that soft takeover is holding the
 * value until the CC value catches the current value.
 */

enum param_mapping_type {
  PARAM_UNMAPPED,
  PARAM_VOICE,
  PARAM_SYNTH,
  PARAM_PENDING
};

/*
 * Soft takeover on/off.  
 */
#define MIDI_CC_SOFT_TAKEOVER (109)

/* API */
void param_midi_map_init(void);
bool param_midi_map_lookup(uint8_t cc, enum param_id *id);
enum param_mapping_type param_set_value_from_cc(uint8_t cc, uint8_t value,
                                                param_value_t params[]);

/*
 * Soft takeover option.  A hardware control's incoming CC value is
 * only applied once it matches/crosses the parameter's current value, so
 * a knob that's out of sync with a loaded patch can't make it jump.
 *
 * OFF by default, set to ON using MIDI_CC_SOFT_TAKEOVER (CC109)
 */
void param_set_soft_takeover(bool enabled);

/*
 * MIDI CC to enum
 */
static inline uint32_t param_cc7bit_to_enum(uint8_t value, uint32_t count) {
  if (count <= 1)
    return 0;

  uint32_t e = ((uint32_t)value * (count - 1) + 63) / 127;

  return e;
}

/*
 * MIDI CC to bool
 */
static inline bool param_cc7bit_to_bool(uint8_t value) { return value >= 64; }

/*
 * MIDI CC to int32
 */
static inline int32_t param_cc7bit_to_int(uint8_t value, int32_t minv,
                                          int32_t maxv) {
  int64_t span = (int64_t)maxv - (int64_t)minv;
  return minv + (int32_t)(((int64_t)value * span + 63) / 127);
}

/*
 * MIDI CC value to unipolar normalized float
 */
static inline float param_cc7bit_to_uni(uint8_t cc) {
  return (float)cc * (1.0f / 127.0f);
}

/*
 * Enum to MIDI CC, used by soft takeover to find the CC value that
 * corresponds to a parameter's current value.
 */
static inline uint8_t param_enum_to_cc7bit(uint32_t e, uint32_t count) {
  if (count <= 1)
    return 0;

  return (uint8_t)(((uint32_t)e * 127 + (count - 1) / 2) / (count - 1));
}

/*
 * Bool to MIDI CC
 */
static inline uint8_t param_bool_to_cc7bit(bool value) {
  return value ? 127 : 0;
}

/*
 * Int32 to MIDI CC
 */
static inline uint8_t param_int_to_cc7bit(int32_t value, int32_t minv,
                                          int32_t maxv) {
  int64_t span = (int64_t)maxv - (int64_t)minv;

  if (span <= 0)
    return 0;

  return (uint8_t)(((int64_t)(value - minv) * 127 + span / 2) / span);
}

/*
 * Unipolar normalized float to MIDI CC
 */
static inline uint8_t param_uni_to_cc7bit(float uni) {
  return (uint8_t)(uni * 127.0f + 0.5f);
}

#endif /* FRUGI_PARAM_MIDI_MAP_H */
