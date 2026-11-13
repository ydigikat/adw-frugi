/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
*/
#ifndef FRUGI_UI_H
#define FRUGI_UI_H

#include "synth.h"
#include <stdbool.h>

bool ui_init(struct synth *s, volatile bool *audio_interrupt_flag);
void ui_routine();

#endif /* FRUGI_UI_H */