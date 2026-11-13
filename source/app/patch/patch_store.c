/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   Patch ADT.  This is a simple list of 32-bit values preceded by a header
   carrying patch meta-data.

   The data is checksummed to detect a corrupt patch, corrupt patches are
   treated as empty and not displayed in the selection list.
   -----------------------------------------------------------------------------
*/
#include <string.h>

#include "patch_store.h"
#include "trace.h"

/* Field offsets */
#define OFF_MAGIC (0u)
#define OFF_VERSION (1u)
#define OFF_PARAM_COUNT (2u)
#define OFF_NAME (4u)
#define OFF_CHECKSUM (20u)
#define OFF_PAYLOAD (PATCH_STORE_HEADER_SIZE)
#define CHECKSUM_HEADER_SPAN_OFFSET (OFF_VERSION)
#define CHECKSUM_HEADER_SPAN_LEN (OFF_CHECKSUM - OFF_VERSION)

/*
 * Read and write functions, all are little-endian.
 */
static void write16(uint8_t *data, uint16_t value) {
  data[0] = (uint8_t)(value & 0xFFu);
  data[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static uint16_t read16(const uint8_t *data) {
  return (uint16_t)(data[0] | ((uint16_t)data[1] << 8));
}

static void write32(uint8_t *data, uint32_t value) {
  data[0] = (uint8_t)(value & 0xFFu);
  data[1] = (uint8_t)((value >> 8) & 0xFFu);
  data[2] = (uint8_t)((value >> 16) & 0xFFu);
  data[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static uint32_t read32(const uint8_t *data) {
  return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
         ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/*
 * Write the param_value_t union as raw bits to the store.
 */
static uint32_t param_to_bits(param_value_t value) {
  uint32_t bits;
  memcpy(&bits, &value, sizeof(bits));
  return bits;
}

static param_value_t param_from_bits(uint32_t bits) {
  param_value_t value;
  memcpy(&value, &bits, sizeof(value));
  return value;
}

/*
 * CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF)
 */
static uint16_t update_crc(uint16_t crc, const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t)((uint16_t)data[i] << 8);

    for (int bit = 0; bit < 8; bit++) {
      crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                            : (uint16_t)(crc << 1);
    }
  }

  return crc;
}

/* Write the checksum into the meta data */
static uint16_t

write_crc(const uint8_t record[PATCH_STORE_SLOT_SIZE]) {
  uint16_t crc = update_crc(0xFFFFu, &record[CHECKSUM_HEADER_SPAN_OFFSET],
                            CHECKSUM_HEADER_SPAN_LEN);
  return update_crc(crc, &record[OFF_PAYLOAD], PATCH_STORE_PAYLOAD_SIZE);
}

/*
 * Returns true if the record's magic byte marks the slot as occupied.
 */
bool patch_store_is_occupied(const uint8_t in[PATCH_STORE_SLOT_SIZE]) {
  RTT_ASSERT(in);
  return in[OFF_MAGIC] == PATCH_STORE_MAGIC;
}

/*
 * Return byte offset of slot within the EEPROM address space.
 */
uint16_t patch_store_slot_addr(uint16_t slot) {
  return (uint16_t)(slot * PATCH_STORE_SLOT_SIZE);
}

/*
 * Packs paramameter & name into the ADT.
 */
bool patch_store_pack(uint8_t out[PATCH_STORE_SLOT_SIZE],
                      const param_value_t params[PARAM_COUNT],
                      const char *name) {
  RTT_ASSERT(out);
  RTT_ASSERT(params);

  size_t name_len = name ? strlen(name) : 0;
  if (name_len >= PATCH_NAME_LEN) {
    return false;
  }

  memset(out, 0, PATCH_STORE_SLOT_SIZE);

  out[OFF_MAGIC] = (uint8_t)PATCH_STORE_MAGIC;
  out[OFF_VERSION] = (uint8_t)PATCH_STORE_FORMAT_VERSION;
  write16(&out[OFF_PARAM_COUNT], (uint16_t)PARAM_COUNT);

  if (name) {
    memcpy(&out[OFF_NAME], name, name_len);
  }

  for (uint32_t i = 0; i < (uint32_t)PARAM_COUNT; i++) {
    write32(&out[OFF_PAYLOAD + i * sizeof(param_value_t)],
            param_to_bits(params[i]));
  }

  write16(&out[OFF_CHECKSUM], write_crc(out));

  return true;
}

/*
 * Checks one stored parameter against its descriptor.
 */
static bool param_bits_valid(uint32_t id, uint32_t bits) {
  if (id == VOICE_PARAM_COUNT) {
    return true; 
  }

  const struct param_desc *desc = &param_desc[id];

  switch (desc->type) {
  case PARAM_TYPE_FLOAT: {
    if (((bits >> 23) & 0xFFu) == 0xFFu) {
      return false; /* exponent is all 1s: this means NaN or infinity */
    }
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f >= 0.0f && f <= 1.0f; /* stored normalised */
  }

  case PARAM_TYPE_ENUM:
    return bits < desc->enum_count; /* enums index jump tables */

  case PARAM_TYPE_INT: {
    int32_t i = (int32_t)bits;
    return i >= desc->min && i <= desc->max;
  }

  case PARAM_TYPE_BOOL:
    return (bits & 0xFFu) <= 1u; /* bool is the union's first byte */

  case PARAM_TYPE_UNDEFINED:
  default:
    return false;
  }
}

/*
 * Extracts a read from EEPROM. Returns false if
 *
 * 1 - the slot is unoccupied,
 * 2 - the format version or param_count don't match this build,
 * 3 - the CRC doesn't match,
 * 4 - params were requested and any value is out of range for its type.
 *
 * Check 4 only applies when params are being loaded.  A record that passes
 * its checksum has a valid name, so a name-only read (the patch browser)
 * still succeeds and the slot can be listed and deleted but it can't be
 * loaded.  On failure the caller's params are left untouched.
 */
bool patch_store_unpack(const uint8_t in[PATCH_STORE_SLOT_SIZE],
                        param_value_t params[PARAM_COUNT],
                        char name_out[PATCH_NAME_LEN]) {
  RTT_ASSERT(in);

  if (!patch_store_is_occupied(in)) {
    return false;
  }

  if (in[OFF_VERSION] != (uint8_t)PATCH_STORE_FORMAT_VERSION) {
    return false;
  }

  if (read16(&in[OFF_PARAM_COUNT]) != (uint16_t)PARAM_COUNT) {
    return false; 
  }

  if (read16(&in[OFF_CHECKSUM]) != write_crc(in)) {
    return false;  /* corrupt patch */
  }

  if (params) {
    /* Validate every value before writing any, so a reject changes nothing */
    for (uint32_t i = 0; i < (uint32_t)PARAM_COUNT; i++) {
      if (!param_bits_valid(
              i, read32(&in[OFF_PAYLOAD + i * sizeof(param_value_t)]))) {
        return false;
      }
    }

    for (uint32_t i = 0; i < (uint32_t)PARAM_COUNT; i++) {
      params[i] =
          param_from_bits(read32(&in[OFF_PAYLOAD + i * sizeof(param_value_t)]));
    }
  }

  if (name_out) {
    memcpy(name_out, &in[OFF_NAME], PATCH_NAME_LEN);
    name_out[PATCH_NAME_LEN - 1] = '\0'; 
  }

  return true;
}
