/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   EEPROM save/load/delete for user patches. This is the save/retrieve layer
   that sits between the application UI and the hardware IO driver.

   All operations are blocking.
   -----------------------------------------------------------------------------
*/

#include "patch_persist.h"

bool patch_persist_init(void) { return eeprom_init(); }

/*
 * Saves params/name into slot.  Blocks for the EEPROM write cycle, up to
 * ~5ms per 64 byte page (~20ms for a whole slot).
 */
bool patch_persist_save(uint16_t slot, const param_value_t params[PARAM_COUNT],
                        const char *name) {
  if (slot >= PATCH_PERSIST_SLOT_COUNT || params == NULL) {
    return false;
  }

  uint8_t record[PATCH_STORE_SLOT_SIZE];

  /* Fails if the name is too long */
  if (!patch_store_pack(record, params, name)) {
    return false;
  }

  return eeprom_write(patch_store_slot_addr(slot), record,
                      PATCH_STORE_SLOT_SIZE);
}

/*
 * Clears slot back to unoccupied.  Only the magic byte needs overwriting.
 */
bool patch_persist_delete(uint16_t slot) {
  if (slot >= PATCH_PERSIST_SLOT_COUNT) {
    return false;
  }

  const uint8_t erased = (uint8_t)PATCH_STORE_ERASED;

  return eeprom_write(patch_store_slot_addr(slot), &erased, 1);
}

/*
 * Loads slot into params/name_out. Returns false if:
 *  1. slot is out of range,
 *  2. the read failed, or
 *  3. the slot holds no valid patch
 */
bool patch_persist_load(uint16_t slot, param_value_t params[PARAM_COUNT],
                        char name_out[PATCH_NAME_LEN]) {
  if (slot >= PATCH_PERSIST_SLOT_COUNT) {
    return false;
  }

  uint8_t record[PATCH_STORE_SLOT_SIZE];
  if (!eeprom_read(patch_store_slot_addr(slot), record,
                   PATCH_STORE_SLOT_SIZE)) {
    return false;
  }

  return patch_store_unpack(record, params, name_out);
}

/*
 * Reads sufficient data to report whether the slot holds a patch.
 *
 * This returns the name for populating the browser page without having to
 * load the entire preset.
 */
bool patch_persist_slot_info(uint16_t slot, bool *occupied,
                             char name_out[PATCH_NAME_LEN]) {
  if (slot >= PATCH_PERSIST_SLOT_COUNT) {
    return false;
  }

  uint8_t record[PATCH_STORE_SLOT_SIZE];
  if (!eeprom_read(patch_store_slot_addr(slot), record,
                   PATCH_STORE_SLOT_SIZE)) {
    return false;
  }

  /*
   * This is the partial load (incl checksum) - a failed checksum record is
   * reported as empty to avoid display of any garbage names.
   */
  bool valid = patch_store_unpack(record, NULL, name_out);

  if (occupied) {
    *occupied = valid;
  }

  if (!valid && name_out) {
    name_out[0] = '\0';
  }

  return true;
}
