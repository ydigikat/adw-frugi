/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
*/
#ifndef FRUGI_BOARD_H
#define FRUGI_BOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ERROR_LOG(s) log_error_halt(s, false)
#define ERROR_HALT(s) log_error_halt(s, true)

bool board_init();
void delay_us(uint16_t us);
void delay_ms(uint16_t ms);
uint16_t tick_us();
void log_error_halt(const char *msg, const bool halt);
/* Implemented by the I2S driver (hardware/io/i2s.c) */
bool audio_start(int16_t audio_buffer[], size_t len,
                 void (*render_audio_callback)(uint8_t buffer_idx));
void led_strobe_on(void);
void led_strobe_off(void);
void board_watchdog_start(void);
void board_watchdog_kick(void);

#endif /* FRUGI_BOARD_H */
