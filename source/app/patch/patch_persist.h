/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
*/
#ifndef FRUGI_PATCH_PERSIST_H
#define FRUGI_PATCH_PERSIST_H

#include <stdbool.h>
#include <stdint.h>

#include "eeprom.h"
#include "param.h"
#include "patch_store.h"

/*
 * EEPROM patch storage.
 *
 * All calls block until complete.  Save and delete take up to ~5ms per
 * 64 byte EEPROM page (~20ms for a full slot), loading and listing are fast
 * reads.
 */

#define PATCH_PERSIST_SLOT_COUNT (EEPROM_SIZE_BYTES / PATCH_STORE_SLOT_SIZE)

/* 
* Slots must start on an EEPROM page so a slot write never spans a page it
* doesn't own (eeprom_write() splits writes at page boundaries) 
*/
static_assert(PATCH_STORE_SLOT_SIZE % EEPROM_PAGE_SIZE == 0,
              "patch slot is not a whole number of EEPROM pages");

bool patch_persist_init(void);
bool patch_persist_save(uint16_t slot, const param_value_t params[PARAM_COUNT],
                        const char *name);
bool patch_persist_delete(uint16_t slot);
bool patch_persist_load(uint16_t slot, param_value_t params[PARAM_COUNT],
                        char name_out[PATCH_NAME_LEN]);
bool patch_persist_slot_info(uint16_t slot, bool *occupied,
                             char name_out[PATCH_NAME_LEN]);

#endif /* FRUGI_PATCH_PERSIST_H */
