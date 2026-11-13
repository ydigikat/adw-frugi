/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   Host-test stand-in for hardware/io/eeprom.h.  patch_persist.h only needs
   the EEPROM geometry; the real header pulls in the SPI bus and ST LL
   headers, which don't exist in the host build.  Keep in step with the real
   values.
   -----------------------------------------------------------------------------
*/
#ifndef FRUGI_EEPROM_H
#define FRUGI_EEPROM_H

#define EEPROM_SIZE_BYTES (32768u)
#define EEPROM_PAGE_SIZE (64u)

#endif /* FRUGI_EEPROM_H */
