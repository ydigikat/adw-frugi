/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
*/

#ifndef FRUGI_SHAPER_H
#define FRUGI_SHAPER_H

/*
 * Single-stage rational wave-shaper
 */
struct shaper {
  float drive_param;
  float drive; /* curve coefficient */
};

/* API */
void shape_init(struct shaper *shaper);
void shape_update(struct shaper *shaper, float drive);
void shape_render(struct shaper *shaper, float sample_block[]);

#endif /* FRUGI_SHAPER_H */
