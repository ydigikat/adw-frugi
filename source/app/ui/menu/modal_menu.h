/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
*/
#ifndef FRUGI_MODAL_MENU_H
#define FRUGI_MODAL_MENU_H

#include <stdint.h>

#include "param.h"


void modal_init(void);

void modal_on_shown(void);
void modal_on_hidden(void);
void modal_next_field(void); 
void modal_prev_field(void); 
void modal_right(void);      
void modal_left(void);       
void modal_encoder(int8_t direction);
bool modal_confirm(const param_value_t current_params[PARAM_COUNT],
                  param_value_t loaded_params[PARAM_COUNT]);

void render_modal(void);

#endif /* FRUGI_MODAL_MENU_H */
