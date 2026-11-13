/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
*/
#ifndef FRUGI_PATCH_STORE_H
#define FRUGI_PATCH_STORE_H

#include <stdbool.h>
#include <stdint.h>

#include "param.h"

/*
 * Patch ADT (all multi-byte fields are little-endian)
 * ------------------------------------------------------------------------------
 *   offset  size  field
 *   0       1     magic            (PATCH_STORE_MAGIC marks an occupied slot)
 *   1       1     schema version
 *   2       2     param count
 *   4       16    name (NUL padded/terminated)
 *   20      2     checksum (CRC-16/CCITT over the version..payload)
 *   22      2     reserved (zero)
 *   24      ...   payload: PARAM_COUNT * 4 bytes (uint32 per param)
 *
 * The load process checks: magic#, schema, param count, CRC, and each
 * value against its param_desc[] type and range. 
 *
 * There is no automatic schema migration.
 * ------------------------------------------------------------------------------
 */

/* Patch name - including NULL terminator */
#define PATCH_NAME_LEN MAX_NAME_LEN

/* 'F" indicates a slot in use */
#define PATCH_STORE_MAGIC (0x46u)

/* factory-erased EEPROM byte */
#define PATCH_STORE_ERASED (0xFFu)

/* Data schema */
#define PATCH_STORE_FORMAT_VERSION (1u)
#define PATCH_STORE_HEADER_SIZE (24u)
#define PATCH_STORE_PAYLOAD_SIZE                                               \
  ((uint32_t)PARAM_COUNT * (uint32_t)sizeof(param_value_t))
#define PATCH_STORE_RECORD_SIZE                                                \
  (PATCH_STORE_HEADER_SIZE + PATCH_STORE_PAYLOAD_SIZE)

/*
 * Slot size must be a ^2 multiple of EEPROM page size (64 bytes).
 */
#define PATCH_STORE_SLOT_SIZE (256u)

static_assert(PATCH_STORE_RECORD_SIZE <= PATCH_STORE_SLOT_SIZE,
              "patch record no longer fits in a patch slot");

bool patch_store_pack(uint8_t out[PATCH_STORE_SLOT_SIZE],
                      const param_value_t params[PARAM_COUNT],
                      const char *name);

bool patch_store_unpack(const uint8_t in[PATCH_STORE_SLOT_SIZE],
                        param_value_t params[PARAM_COUNT],
                        char name_out[PATCH_NAME_LEN]);

bool patch_store_is_occupied(const uint8_t in[PATCH_STORE_SLOT_SIZE]);
uint16_t patch_store_slot_addr(uint16_t slot);

#endif /* FRUGI_PATCH_STORE_H */
