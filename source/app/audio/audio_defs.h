/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   * Compile-time audio constants shared between the audio task and the
   * synth engine.  Buffers on both sides are statically allocated so
   * these need to be constants rather than parameters.
   *
   * This is separated so that there are no hardware related includes which
   * leak into the generic C code for the synth. 
   -----------------------------------------------------------------------------
*/
#ifndef FRUGI_AUDIO_DEFS_H
#define FRUGI_AUDIO_DEFS_H

#define PING (0)
#define PONG (1)

#ifndef AUDIO_SAMPLE_RATE
#define AUDIO_SAMPLE_RATE (48000U) 
#endif

#ifndef AUDIO_BLOCK_SIZE
#define AUDIO_BLOCK_SIZE (128U)
#endif

#endif /* FRUGI_AUDIO_DEFS_H */
