/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
*/
#ifndef FRUGI_NOISE_H
#define FRUGI_NOISE_H

#include <stdbool.h>

struct noise {
  float level;
  float gain;

  bool note_on;
};

void noise_init(struct noise *noise, float gain);
void noise_reset(struct noise *noise);
void noise_render(struct noise *noise, float sample_block[]);
void noise_note_on(struct noise *noise);
void noise_note_off(struct noise *noise);
void noise_update(struct noise *noise, float level);

#endif /* FRUGI_NOISE_H */