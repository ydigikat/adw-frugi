/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   The patch menu is modal and accessed from the ring-menu by a double-click
   of the rotary pushbutton (C).  A second double-click returns to the ring
   menu.

   It allows the user to browse/load/save/delete patches.

   During operation the buttons are mapped differently:

   N-S  : move between the SLOT (patch) and ACTION fields.
   E-W  : moves +10 or -10 patches, or move the name-entry cursor.
   encoder: adjust the active field or cycle a name character.
   C-HOLD: confirm (go/save/delete, or to commit a typed name).
   double-click C : dismiss page.
   -----------------------------------------------------------------------------
*/
#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "display.h"
#include "modal_menu.h"
#include "patch_persist.h"

enum modal_field { MODAL_FIELD_SLOT, MODAL_FIELD_ACTION };
enum modal_action {
  MODAL_ACTION_LOAD,
  MODAL_ACTION_SAVE,
  MODAL_ACTION_DELETE,
  MODAL_ACTION_COUNT
};

enum modal_mode {
  MODAL_MODE_BROWSE,
  MODAL_MODE_NAME_ENTRY,
  MODAL_MODE_CONFIRM_DELETE,
};

static const char *const action_names[MODAL_ACTION_COUNT] = {
    [MODAL_ACTION_LOAD] = "LOAD",
    [MODAL_ACTION_SAVE] = "SAVE",
    [MODAL_ACTION_DELETE] = "DELETE",
};

/* 
* Characters available for patch naming - start with a space since that will
* cause the encoder to cycle from a blank straight onto A,B etc.
*/
static const char name_charset[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-";
#define NAME_CHARSET_LEN ((int)sizeof(name_charset) - 1)

struct modal_state {
  enum modal_mode mode;
  enum modal_field field;
  uint16_t slot;
  enum modal_action action;

  bool slot_occupied;
  char slot_name[PATCH_NAME_LEN];
  char edit_name[PATCH_NAME_LEN];
  uint8_t edit_pos;

  const char *status;
};

static struct modal_state state;
static bool dirty;

static void refresh_slot_info(void) {
  if (!patch_persist_slot_info(state.slot, &state.slot_occupied,
                               state.slot_name)) {
    state.slot_occupied = false;
    state.slot_name[0] = '\0';
  }
}

static void jump_slot(int32_t delta) {
  int32_t next = (int32_t)state.slot + delta;
  next = ((next % PATCH_PERSIST_SLOT_COUNT) + PATCH_PERSIST_SLOT_COUNT) %
         PATCH_PERSIST_SLOT_COUNT;
  state.slot = (uint16_t)next;
  state.status = NULL;
  refresh_slot_info();
  dirty = true;
}

/*
* Space pads the name so that the cursor can move across the entire
* field and we don't show garbage.
*/   
static void init_edit_name(const char *src) {
  size_t i = 0;
  for (; src != NULL && src[i] != '\0' && i < PATCH_NAME_LEN - 1; i++) {
    state.edit_name[i] = src[i];
  }
  for (; i < PATCH_NAME_LEN - 1; i++) {
    state.edit_name[i] = ' ';
  }
  state.edit_name[PATCH_NAME_LEN - 1] = '\0';
  state.edit_pos = 0;
}

static void trim_trailing_spaces(char *s) {
  size_t len = strlen(s);
  while (len > 0 && s[len - 1] == ' ') {
    s[--len] = '\0';
  }
}

static int charset_index_of(char c) {
  for (int i = 0; i < NAME_CHARSET_LEN; i++) {
    if (name_charset[i] == c) {
      return i;
    }
  }
  return 0;
}

/*
* Anything other than a long-press abandons the confirmation.
*/
static bool cancel_confirm_delete(void) {
  if (state.mode != MODAL_MODE_CONFIRM_DELETE) {
    return false;
  }

  state.mode = MODAL_MODE_BROWSE;
  dirty = true;
  return true;
}

void modal_init(void) {
  state.mode = MODAL_MODE_BROWSE;
  state.field = MODAL_FIELD_SLOT;
  state.slot = 0;
  state.action = MODAL_ACTION_LOAD;
  state.status = NULL;
  refresh_slot_info();
  dirty = true;
}

void modal_on_shown(void) {
  refresh_slot_info();
  dirty = true;
}

/*
* Abandoning half-way through name entry/delete confirmation will cancel. 
*/
void modal_on_hidden(void) {
  if (state.mode == MODAL_MODE_NAME_ENTRY ||
      state.mode == MODAL_MODE_CONFIRM_DELETE) {
    state.mode = MODAL_MODE_BROWSE;
    state.status = NULL;
  }
}

void modal_next_field(void) {
  if (cancel_confirm_delete() || state.mode != MODAL_MODE_BROWSE) {
    return;
  }

  state.field =
      (state.field == MODAL_FIELD_SLOT) ? MODAL_FIELD_ACTION : MODAL_FIELD_SLOT;
  state.status = NULL;
  dirty = true;
}

void modal_prev_field(void) {
  modal_next_field(); 
}

void modal_right(void) {
  if (cancel_confirm_delete()) {
    return;
  }

  if (state.mode == MODAL_MODE_NAME_ENTRY) {
    if (state.edit_pos < PATCH_NAME_LEN - 2) {
      state.edit_pos++;
    }
    dirty = true;
    return;
  }

  if (state.mode == MODAL_MODE_BROWSE) {
    jump_slot(10);
  }
}

void modal_left(void) {
  if (cancel_confirm_delete()) {
    return;
  }

  if (state.mode == MODAL_MODE_NAME_ENTRY) {
    if (state.edit_pos > 0) {
      state.edit_pos--;
    }
    dirty = true;
    return;
  }

  if (state.mode == MODAL_MODE_BROWSE) {
    jump_slot(-10);
  }
}

void modal_encoder(int8_t direction) {
  if (cancel_confirm_delete()) {
    return;
  }

  if (state.mode == MODAL_MODE_NAME_ENTRY) {
    int idx = charset_index_of(state.edit_name[state.edit_pos]);
    idx = (idx + direction + NAME_CHARSET_LEN) % NAME_CHARSET_LEN;
    state.edit_name[state.edit_pos] = name_charset[idx];
    dirty = true;
    return;
  }

  if (state.mode != MODAL_MODE_BROWSE) {
    return;
  }

  if (state.field == MODAL_FIELD_SLOT) {
    jump_slot(direction);
    return;
  }

  int next =
      ((int)state.action + direction + MODAL_ACTION_COUNT) % MODAL_ACTION_COUNT;
  state.action = (enum modal_action)next;
  state.status = NULL;
  dirty = true;
}

/*
 * Handle confirm gesture (long-press C).
 */
bool modal_confirm(const param_value_t current_params[PARAM_COUNT],
                   param_value_t loaded_params[PARAM_COUNT]) {
  switch (state.mode) {

  /*
  * Actions available when browsing the patch list.
  */
  case MODAL_MODE_BROWSE:
    switch (state.action) {

    /* Load the selected patch */
    case MODAL_ACTION_LOAD:
      dirty = true;
      if (patch_persist_load(state.slot, loaded_params, state.slot_name)) {
        state.slot_occupied = true;
        state.status = "Loaded.";
        return true;
      }
      state.status = "Load failed.";
      return false;

    /* Save the patch - this allows the user to change the name */
    case MODAL_ACTION_SAVE:
      init_edit_name(state.slot_occupied ? state.slot_name : "");
      state.mode = MODAL_MODE_NAME_ENTRY;
      state.status = NULL;
      dirty = true;
      return false;

    /* Delete the patch - this requires a long-press confirmation */
    case MODAL_ACTION_DELETE:
      state.mode = MODAL_MODE_CONFIRM_DELETE;
      state.status = NULL;
      dirty = true;
      return false;

    default:
      return false;
    }

  /*
  * Create/edit patch name.  Confirming the name saves the patch.
  */
  case MODAL_MODE_NAME_ENTRY: {
    char name[PATCH_NAME_LEN];
    memcpy(name, state.edit_name, PATCH_NAME_LEN);
    trim_trailing_spaces(name);

    /*
     * Blocking - brief stall (~20ms) is acceptable for a user save, but
     * silence the output first so the stall is heard as silence rather than
     * the DMA looping its last buffer (see audio_silence()).
     */
    audio_silence();
    bool ok = patch_persist_save(state.slot, current_params, name);
    state.status = ok ? "Saved." : "Save failed.";
    state.mode = MODAL_MODE_BROWSE;
    refresh_slot_info();
    dirty = true;
    return false;
  }

  /*
  * Confirm delete patch.  Confirming this will delete the patch.
  */
  case MODAL_MODE_CONFIRM_DELETE: {
    /* Blocks for one EEPROM write cycle (~5ms) - silence as for save */
    audio_silence();
    bool ok = patch_persist_delete(state.slot);
    state.status = ok ? "Deleted." : "Delete failed.";
    state.mode = MODAL_MODE_BROWSE;
    refresh_slot_info();
    dirty = true;
    return false;
  }

  default:
    return false;
  }
}

/*
* Clear and write a row of display data.
*/
static void write_row(uint8_t row, const char *text, uint16_t fg, uint16_t bg) {
  char blank[DISPLAY_COLS + 1];
  memset(blank, ' ', DISPLAY_COLS);
  blank[DISPLAY_COLS] = '\0';
  display_write_string(0, row, blank, fg, bg);

  if (text) {
    display_write_string(0, row, text, fg, bg);
  }
}

/* 
* Write a field.  Label and inverted text for data.
*/
static void write_field_row(uint8_t row, const char *label, const char *value,
                            bool selected) {
  uint16_t fg = selected ? COLOUR_BG : COLOUR_FG;
  uint16_t bg = selected ? COLOUR_FG : COLOUR_BG;

  write_row(row, label, fg, bg);
  display_write_string(DISPLAY_COLS - (uint8_t)strlen(value), row, value, fg,
                       bg);
}

/*
* Render the page.
*/
void render_modal(void) {
  if (!dirty) {
    return;
  }

  char value[8];

  write_row(0, "LOAD/SAVE PATCH", COLOUR_FG, COLOUR_BG);

  snprintf(value, sizeof(value), "%03u", state.slot);
  write_field_row(2, "Patch #", value,
                  state.mode == MODAL_MODE_BROWSE &&
                      state.field == MODAL_FIELD_SLOT);

  write_row(3, state.slot_occupied ? state.slot_name : "-- empty --", COLOUR_FG,
            COLOUR_BG);

  write_field_row(4, "Action", action_names[state.action],
                  state.mode == MODAL_MODE_BROWSE &&
                      state.field == MODAL_FIELD_ACTION);

  switch (state.mode) {
  case MODAL_MODE_BROWSE: {
    char hint[DISPLAY_COLS + 1];
    snprintf(hint, sizeof(hint), "Hold C %s", action_names[state.action]);
    write_row(6, NULL, COLOUR_FG, COLOUR_BG);
    write_row(7, hint, COLOUR_FG, COLOUR_BG);
    write_row(8, state.status, COLOUR_FG, COLOUR_BG);
    break;
  }

  case MODAL_MODE_NAME_ENTRY:
    write_row(6, state.edit_name, COLOUR_FG, COLOUR_BG);
    display_put_char(state.edit_pos, 6,
                     (unsigned char)state.edit_name[state.edit_pos], COLOUR_BG,
                     COLOUR_FG);
    write_row(7, "L/R,Hold C Save", COLOUR_FG, COLOUR_BG);
    write_row(8, NULL, COLOUR_FG, COLOUR_BG);
    break;

  case MODAL_MODE_CONFIRM_DELETE: {
    char prompt[DISPLAY_COLS + 1];
    snprintf(prompt, sizeof(prompt), "Delete slot %03u?", state.slot);
    write_row(6, prompt, COLOUR_FG, COLOUR_BG);
    write_row(7, "Hold C=yes, else no", COLOUR_FG, COLOUR_BG);
    write_row(8, NULL, COLOUR_FG, COLOUR_BG);
    break;
  }
  }

  dirty = false;
}
