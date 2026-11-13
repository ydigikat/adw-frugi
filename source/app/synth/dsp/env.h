/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
*/

#ifndef FRUGI_ENV_GEN_H
#define FRUGI_ENV_GEN_H

#include <stdalign.h>
#include <stddef.h>

#include "param.h"

enum env_state {
  ENV_OFF,
  ENV_ATTACK,
  ENV_DECAY,
  ENV_SUSTAIN,
  ENV_RELEASE,
  ENV_SHUTDOWN,
  ENV_MAX
};

struct env {
  /* Params */
  float attack;
  float decay;
  float sustain;
  float release;
  uint8_t mode;
  enum mod_source mod_source;
  float mod_depth;
  bool note_track;
  bool velocity_track;

  /* State */
  enum env_state state;
  float level;
  float attack_coeff;
  float decay_coeff;
  float release_coeff;
  float attack_tco;
  float decay_tco;
  float release_tco;
  float attack_overshoot;
  float decay_overshoot;
  float release_overshoot;
  float inc_shutdown;
  float attack_scaler;
  float decay_scaler;
};

void env_init(struct env *env);
void env_reset(struct env *env);
void env_render(struct env *env, float *output);
void env_note_on(struct env *env, uint8_t midi_note, uint8_t midi_velocity);
void env_note_off(struct env *env);
void env_rtz(struct env *env);
void env_update(struct env *env, float attack, float decay, float sustain,
                float release, enum env_mode mode, bool note_track,
                bool velocity_track);

#endif /* FRUGI_ENV_GEN_H*/
