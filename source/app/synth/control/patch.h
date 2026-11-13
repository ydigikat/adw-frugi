/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
*/
#ifndef FRUGI_DEFAULT_PATCH_H
#define FRUGI_DEFAULT_PATCH_H

#include "param.h"

/*
 * There is no hard-coded factory bank, only a default.  The full build allows
 * save/recall of patches to EEPROM.
 */
void param_create_default_patch(param_value_t params[]);

#endif /* FRUGI_DEFAULT_PATCH_H */
