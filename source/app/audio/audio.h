/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
*/
#ifndef FRUGI_AUDIO_H
#define FRUGI_AUDIO_H

#include <stdbool.h>
#include <stdint.h>


/*
 * Block render handler, called once per audio block.
 */
typedef void (*audio_block_fn)(void *ctx, float *left, float *right);

bool audio_init(audio_block_fn process_block_function, void *ctx,
                volatile bool *audio_interrupt_flag);
void audio_routine();
void audio_silence(void);
uint32_t audio_underrun_count(void);

#endif /* FRUGI_AUDIO_H */