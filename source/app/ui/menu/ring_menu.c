/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   Parameter pages are presented as a continous ring, the E-W buttons move
   between pages which wrap around at the first and last.  This menu is
   modeless and intended to be used to adjust controls while playing.

   This is distinct from the modal menu - used to display the utilities and
   patch selection pages.

   Each page is simply a list of fields, one field per page row. The user can
   scroll through the fields using the up/down buttons.  Moving the encoder
   will immediately change the associated value.

   The status bar (line 1 of the LCD) shows the previous, current and next
   page titles to aid in navigation.

   All the mapping of parameters to UI display and updating of values is
   handled in this module.
*/

#include "ring_menu.h"
#include "display.h"
#include "param.h"
#include <stdio.h>
#include <string.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

/*
 * Shared modulation source labels
 */
const char *const mod_source_names[MOD_SOURCE_COUNT] = {
    [LFO1_TRI] = "TRI",       [LFO1_SAW] = "SAW",   [LFO1_RAMP] = "RAMP",
    [LFO1_SQUARE] = "SQUARE", [LFO1_SANDH] = "S&H", [ENV1_LEVEL] = "ENV",
};

/*
 * DCO waveform labels
 */
static const char *const dco_wave_names[DCO_WAVE_COUNT] = {
    [DCO_TRI] = "TRI",
    [DCO_SAW] = "SAW",
    [DCO_PULSE] = "PULSE",
};

/*
 * Filter modes
 */
static const char *const filter_mode_names[FILTER_COUNT] = {
    [FILTER_LPF2] = "LP2", [FILTER_BPF2] = "BP2", [FILTER_HPF2] = "HP2",
    [FILTER_LPF4] = "LP4", [FILTER_BPF4] = "BP4", [FILTER_HPF4] = "HP4",
};

/*
 * DCO waveforms
 */
static const char *const env_mode_names[ENV_MODE_COUNT] = {
    [ENV_NORMAL] = "NORMAL",
    [ENV_INVERTED] = "INV",
    [ENV_BIASED] = "BIAS",
    [ENV_INVERTED_BIASED] = "INV+BIAS"};

/*
 * LFO modes
 */
static const char *const lfo_mode_names[LFO_MODE_COUNT] = {
    [LFO_MODE_NOTE] = "NOTE", [LFO_MODE_FREE] = "FREE"};

/*
 * VOICE modes
 */
static const char *const voice_mode_names[VOICE_MODE_COUNT] = {
    [VOICE_MODE_MONO] = "MONO", [VOICE_MODE_POLY] = "POLY"};

/*
 * MOD-Wheel destinations
 */
static const char *const mod_dest_names[MOD_WHEEL_DEST_COUNT] = {
    [MOD_WHEEL_DEST_VCO] = "VCO",
    [MOD_WHEEL_DEST_VCF] = "VCF",
    [MOD_WHEEL_DEST_VCO_VCF] = "VCO+VCF"};

/*DCO1 page 1 - primary functions */
static const struct ring_field dco1_fields[] = {
    {.id = DCO1_WAVE, .label = "Wave", .row = 1, .enum_names = dco_wave_names},
    {.id = DCO1_LEVEL, .label = "Level", .row = 2},
    {.id = DCO1_OCTAVE, .label = "Octave", .row = 3},
    {.id = DCO1_SEMI, .label = "Semi", .row = 4},
    {.id = DCO1_CENTS, .label = "Cents", .row = 5},
    {.id = DCO1_PULSEWIDTH, .label = "Pulse Width", .row = 6}};

/* DCO1 page 2 - modulators */
static const struct ring_field dco1mod_fields[] = {
    {.id = DCO1_MOD_SOURCE,
     .label = "Mod Source",
     .row = 1,
     .enum_names = mod_source_names},
    {.id = DCO1_MOD_DEPTH, .label = "Mod Depth", .row = 2},
    {.id = DCO1_PWM_SOURCE,
     .label = "PWM Source",
     .row = 3,
     .enum_names = mod_source_names},
    {.id = DCO1_PWM_DEPTH, .label = "PWM Depth", .row = 4},
};

/*DCO2 page 1 - primary functions */
static const struct ring_field dco2_fields[] = {
    {.id = DCO2_WAVE, .label = "Wave", .row = 1, .enum_names = dco_wave_names},
    {.id = DCO2_LEVEL, .label = "Level", .row = 2},
    {.id = DCO2_OCTAVE, .label = "Octave", .row = 3},
    {.id = DCO2_SEMI, .label = "Semi", .row = 4},
    {.id = DCO2_CENTS, .label = "Cents", .row = 5},
    {.id = DCO2_PULSEWIDTH, .label = "Pulse Width", .row = 6}};

/* DCO2 page 2 - modulators */
static const struct ring_field dco2mod_fields[] = {
    {.id = DCO2_MOD_SOURCE,
     .label = "Mod Source",
     .row = 1,
     .enum_names = mod_source_names},
    {.id = DCO2_MOD_DEPTH, .label = "Mod Depth", .row = 2},
    {.id = DCO2_PWM_SOURCE,
     .label = "PWM Source",
     .row = 3,
     .enum_names = mod_source_names},
    {.id = DCO2_PWM_DEPTH, .label = "PWM Depth", .row = 4},
};

/*
 * Ring-mod hint.  Ring modulation only sounds like ring mod when DCO2 sits
 * at an inharmonic interval to DCO1: at unison/octaves the product only adds
 * harmonics already present, and at a fifth (3:2) the sum and difference
 * tones are harmonics of a lower note, so it just sounds like a sub-octave.
 * So, with the ring mod in use:
 *  - DCO2 at a weak interval (unison/octave or fifth, within a few cents):
 *    prompt to detune it;
 *  - otherwise, if DCO1 is still in the mix, point out that muting it gives
 *    pure ring mod (DCO1's level doesn't feed the ring - see dco_mix()).
 */
#define RINGMOD_WEAK_CENTS (3.5f)

static const char *ringmod_hint(const param_value_t *v) {
  if (v[DCO_RINGMOD_LEVEL].f <= 0.0f) {
    return NULL;
  }

  /* DCO2's interval above DCO1, in semitones (folded to an octave) and cents */
  int32_t semis = (v[DCO2_OCTAVE].i - v[DCO1_OCTAVE].i) * 12 +
                  (v[DCO2_SEMI].i - v[DCO1_SEMI].i);
  float cents = (v[DCO2_CENTS].f - v[DCO1_CENTS].f) * (2.0f * OSC_CENTS_RANGE);
  int32_t folded = ((semis % 12) + 12) % 12;
  bool in_tune = cents < RINGMOD_WEAK_CENTS && cents > -RINGMOD_WEAK_CENTS;

  if (in_tune && (folded == 0 || folded == 7)) {
    return "Detune DCO2 to ring";
  }

  if (v[DCO1_LEVEL].f > 0.0f) {
    return "DCO1 Lvl 0: pure RM";
  }

  return NULL;
}

/* NOISE/Shaper page*/
static const struct ring_field noise_shaper_fields[] = {
    {.id = DCO_DRIVE, .label = "OSC Drive", .row = 1},
    {.id = NOISE_LEVEL, .label = "Noise Level", .row = 2},
    {.id = DCO_RINGMOD_LEVEL,
     .label = "Ring-Mod",
     .row = 3,
     .hint = ringmod_hint}};

/* DCF filter page */
static const struct ring_field dcf_fields[] = {
    {.id = DCF_CUTOFF, .label = "Cutoff", .row = 1},
    {.id = DCF_RES, .label = "Resonance", .row = 2},
    {.id = DCF_SAT, .label = "Saturation", .row = 3},
    {.id = DCF_MODE,
     .label = "Mode",
     .row = 4,
     .enum_names = filter_mode_names},
    {.id = DCF_TRACK, .label = "Track", .row = 5},
    {.id = DCF_MOD_SOURCE,
     .label = "Mod Source",
     .row = 6,
     .enum_names = mod_source_names},
    {.id = DCF_MOD_DEPTH, .label = "Mod Depth", .row = 7},

};

/* Modulation envelope generator page */
static const struct ring_field mod_env_fields[] = {
    {.id = ENV1_ATTACK, .label = "Attack", .row = 1},
    {.id = ENV1_DECAY, .label = "Decay", .row = 2},
    {.id = ENV1_SUSTAIN, .label = "Sustain", .row = 3},
    {.id = ENV1_RELEASE, .label = "Release", .row = 4},
    {.id = ENV1_MODE, .label = "Mode", .row = 5, .enum_names = env_mode_names}};

/* LFO page */
static const struct ring_field mod_lfo_fields[] = {
    {.id = LFO1_RATE, .label = "LFO1 Rate", .row = 1},
    {.id = LFO1_MODE,
     .label = "LFO1 Mode",
     .row = 2,
     .enum_names = lfo_mode_names}};

/* DCA page */
static const struct ring_field dca_fields[] = {
    {.id = DCA_LEVEL, .label = "Level", .row = 1},
    {.id = DCA_ATTACK, .label = "Attack", .row = 2},
    {.id = DCA_DECAY, .label = "Decay", .row = 3},
    {.id = DCA_SUSTAIN, .label = "Sustain", .row = 4},
    {.id = DCA_RELEASE, .label = "Release", .row = 5},
    {.id = DCA_MOD_SOURCE,
     .label = "MOD Source",
     .row = 6,
     .enum_names = mod_source_names},
    {.id = DCA_MOD_DEPTH, .label = "MOD Depth", .row = 7},
    {.id = DCA_NOTE_TRACK, .label = "Note Track", .row = 8},
    {.id = DCA_VELOCITY_TRACK, .label = "Vel Track", .row = 9}};

/* Performance page */
static const struct ring_field perf_fields[] = {
    {.id = SYNTH_CHORUS_ON, .label = "Chorus", .row = 1},
    {.id = SYNTH_MOD_WHEEL_DEST,
     .label = "Mod Wheel",
     .row = 2,
     .enum_names = mod_dest_names},
    {.id = SYNTH_VOICE_MODE,
     .label = "Voice Mode",
     .row = 3,
     .enum_names = voice_mode_names},
    {.id = SYNTH_PORTAMENTO_ON, .label = "Portamento", .row = 4},
    {.id = SYNTH_PORTAMENTO_TIME, .label = "Time", .row = 5},
    {.id = SYNTH_UNISON_DETUNE, .label = "Uni-Detune", .row = 6},
    {.id = SYNTH_BEND_RANGE, .label = "Bend", .row = 7}};

/*
 * Float fields are stored as semi-normalised (0..1), the UI has no
 * understanding of their meaning, only the DSP module knows this, so the UI
 * only displays it as a range value, 0-100.
 */
static void render_float(const struct ring_field *field, param_value_t value,
                         char *out, size_t out_len) {
  (void)field;
  snprintf(out, out_len, "%3d%%", (int)(value.f * 100.0f + 0.5f));
}

static void render_int(const struct ring_field *field, param_value_t value,
                       char *out, size_t out_len) {
  (void)field;
  snprintf(out, out_len, "%+ld", (long)value.i);
}

static void render_bool(const struct ring_field *field, param_value_t value,
                        char *out, size_t out_len) {
  (void)field;
  snprintf(out, out_len, "%s", value.b ? "ON" : "OFF");
}

static void render_enum(const struct ring_field *field, param_value_t value,
                        char *out, size_t out_len) {
  RTT_ASSERT(field->enum_names);
  snprintf(out, out_len, "%s", field->enum_names[value.e]);
}

/*
 * Associate render function to each type (jump-table)
 */
typedef void (*value_render_fn)(const struct ring_field *field,
                                param_value_t value, char *out, size_t out_len);

static const value_render_fn value_renderers[] = {
    [PARAM_TYPE_FLOAT] = render_float,
    [PARAM_TYPE_ENUM] = render_enum,
    [PARAM_TYPE_BOOL] = render_bool,
    [PARAM_TYPE_INT] = render_int,
};

/*
 * Value steppers - in a similar way to the renderers, these increment/decrement
 * each type.
 */
typedef param_value_t (*value_step_fn)(const struct param_desc *desc,
                                       param_value_t current, int8_t direction);

static param_value_t step_float(const struct param_desc *desc,
                                param_value_t current, int8_t direction) {
  (void)desc;
  float v = current.f + (float)direction * 0.01f;
  current.f = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
  return current;
}

static param_value_t step_int(const struct param_desc *desc,
                              param_value_t current, int8_t direction) {
  int32_t v = current.i + direction;
  current.i = v < desc->min ? desc->min : (v > desc->max ? desc->max : v);
  return current;
}

static param_value_t step_bool(const struct param_desc *desc,
                               param_value_t current, int8_t direction) {
  /* A bool has no "direction" - either click just flips it. */
  (void)desc;
  (void)direction;
  current.b = !current.b;
  return current;
}

static param_value_t step_enum(const struct param_desc *desc,
                               param_value_t current, int8_t direction) {
  int32_t v = (int32_t)current.e + direction;
  if (v < 0) {
    v = (int32_t)desc->enum_count - 1;
  } else if (v >= (int32_t)desc->enum_count) {
    v = 0;
  }
  current.e = (uint32_t)v;
  return current;
}

/*
 * Associate each stepper function to a type (jump-table)
 */
static const value_step_fn value_steppers[] = {
    [PARAM_TYPE_FLOAT] = step_float,
    [PARAM_TYPE_ENUM] = step_enum,
    [PARAM_TYPE_BOOL] = step_bool,
    [PARAM_TYPE_INT] = step_int,
};

/*
 * The page ring navigation definition, this also sets the order in which
 * pages are traversed.
 */
/*
 * Field counts are taken from the field arrays (ARRAY_LEN) so they cannot
 * drift from the tables.  Titles are padded to 5 characters to keep the
 * status bar aligned.
 */
const struct ring_page ring_pages[RING_PAGE_COUNT] = {
    [RING_PAGE_DCO1] = {.title = "DCO1 ",
                        .field_count = ARRAY_LEN(dco1_fields),
                        .fields = dco1_fields},
    [RING_PAGE_MOD1] = {.title = "MOD1 ",
                        .field_count = ARRAY_LEN(dco1mod_fields),
                        .fields = dco1mod_fields},
    [RING_PAGE_DCO2] = {.title = "DCO2 ",
                        .field_count = ARRAY_LEN(dco2_fields),
                        .fields = dco2_fields},
    [RING_PAGE_MOD2] = {.title = "MOD2 ",
                        .field_count = ARRAY_LEN(dco2mod_fields),
                        .fields = dco2mod_fields},
    [RING_PAGE_NOISE] = {.title = "NOISE",
                         .field_count = ARRAY_LEN(noise_shaper_fields),
                         .fields = noise_shaper_fields},
    [RING_PAGE_DCF] = {.title = "DCF  ",
                       .field_count = ARRAY_LEN(dcf_fields),
                       .fields = dcf_fields},
    [RING_PAGE_MOD_EG] = {.title = "M-ENV",
                          .field_count = ARRAY_LEN(mod_env_fields),
                          .fields = mod_env_fields},
    [RING_PAGE_LFO] = {.title = "LFO  ",
                       .field_count = ARRAY_LEN(mod_lfo_fields),
                       .fields = mod_lfo_fields},
    [RING_PAGE_DCA] = {.title = "DCA  ",
                       .field_count = ARRAY_LEN(dca_fields),
                       .fields = dca_fields},
    [RING_PAGE_PERF] = {.title = "PERF ",
                        .field_count = ARRAY_LEN(perf_fields),
                        .fields = perf_fields}};

/*
 * Renders the breadcrumb trail that shows the prev/current/next pages
 * in the ring.  This is shown on row 0 as a status bar.
 */
static void render_status_bar(enum ring_page_id current_page) {
  char status[DISPLAY_COLS + 1];

  enum ring_page_id prev =
      (current_page == 0) ? RING_PAGE_COUNT - 1 : current_page - 1;

  enum ring_page_id next =
      (current_page == RING_PAGE_COUNT - 1) ? 0 : current_page + 1;

  snprintf(status, sizeof(status), "[%s]<%s %s>",
           ring_pages[current_page].title, ring_pages[prev].title,
           ring_pages[next].title);

  display_write_string(0, 0, status, COLOUR_WHITE, COLOUR_MIDNIGHTBLUE);
}

/*
 * Renders a field, calling the render function to convert the value to a
 * string.
 */
static void render_field(const struct ring_field *field,
                         const param_value_t *values, bool selected) {
  uint16_t fg = selected ? COLOUR_BG : COLOUR_FG;
  uint16_t bg = selected ? COLOUR_FG : COLOUR_BG;

#ifdef TRACE_UI
  RTT_LOG("Display field id=%d [row=%d]\n", field->id, field->row);
#endif

  /* Render value into string - sized to the row, so no value is truncated */
  char value_str[DISPLAY_COLS + 1];

  /* Call render function via jump table based on type */
  value_renderers[param_desc[field->id].type](field, values[field->id],
                                              value_str, sizeof(value_str));

  /* Write blank string to clear any previous line */
  char row_blank[DISPLAY_COLS + 1];
  memset(row_blank, ' ', DISPLAY_COLS);
  row_blank[DISPLAY_COLS] = '\0';
  display_write_string(0, field->row, row_blank, fg, bg);

  /* Render the label and field */
  display_write_string(0, field->row, field->label, fg, bg);
  display_write_string(DISPLAY_COLS - (uint8_t)strlen(value_str), field->row,
                       value_str, fg, bg);
}

/*
 * Renders the selected field's hint (if any) on the hint row, dimmed so it
 * doesn't read as another field.  The row is cleared on every redraw, so a
 * hint disappears as soon as it no longer applies.  Pages with a field on
 * the hint row are left alone.
 */
static void render_hint(const struct ring_page *page,
                        const struct ring_field *selected,
                        const param_value_t *values) {
  for (uint8_t i = 0; i < page->field_count; i++) {
    if (page->fields[i].row == RING_HINT_ROW) {
      return;
    }
  }

  char hint_str[DISPLAY_COLS + 1];
  const char *hint = selected->hint ? selected->hint(values) : NULL;

  /* Pad to the full row so any previous hint is overwritten */
  snprintf(hint_str, sizeof(hint_str), "%-*s", DISPLAY_COLS, hint ? hint : "");
  display_write_string(0, RING_HINT_ROW, hint_str, COLOUR_DARKGRAY, COLOUR_BG);
}

/*
 * Renders the ring menu page, displaying the current parameter values held
 * by the synth.
 */
void render_ring(struct ring_state *ring_state, const param_value_t *values) {
  RTT_ASSERT(ring_state);
  RTT_ASSERT(values);
  RTT_ASSERT(ring_state->page < RING_PAGE_COUNT);

  /* Only redraw page if something has changed */
  if (get_param_changed()) {
    /* Update the breadcrumb trail to show where we are */
    render_status_bar(ring_state->page);

    const struct ring_page *page = &ring_pages[ring_state->page];

#ifdef TRACE_UI
    RTT_LOG("Rendering page %s\n", page->title);
    RTT_LOG("%d fields total\n", page->field_count);
#endif

    for (uint8_t i = 0; i < page->field_count; i++) {
      render_field(&page->fields[i], values, i == ring_state->sel_field);
    }

    render_hint(page, &page->fields[ring_state->sel_field], values);

    /* We've seen it now - reset */
    set_param_changed(false);
  }
}

/*
 * Initialises the ring menu state.
 */
void ring_init(struct ring_state *ring_state) {
  ring_state->page = RING_PAGE_DCF;
  ring_state->sel_field = 0;
}

void ring_next_page(struct ring_state *ring_state) {
  RTT_ASSERT(ring_state);
  ring_state->page =
      (ring_state->page == RING_PAGE_COUNT - 1) ? 0 : ring_state->page + 1;
  ring_state->sel_field = 0;
  set_param_changed(true);
}

void ring_prev_page(struct ring_state *ring_state) {
  RTT_ASSERT(ring_state);
  ring_state->page =
      (ring_state->page == 0) ? RING_PAGE_COUNT - 1 : ring_state->page - 1;
  ring_state->sel_field = 0;
  set_param_changed(true);
}

void ring_next_field(struct ring_state *ring_state) {
  RTT_ASSERT(ring_state);
  uint8_t count = ring_pages[ring_state->page].field_count;
  ring_state->sel_field =
      (ring_state->sel_field == count - 1) ? 0 : ring_state->sel_field + 1;
  set_param_changed(true);
}

void ring_prev_field(struct ring_state *ring_state) {
  RTT_ASSERT(ring_state);
  uint8_t count = ring_pages[ring_state->page].field_count;
  ring_state->sel_field =
      (ring_state->sel_field == 0) ? count - 1 : ring_state->sel_field - 1;
  set_param_changed(true);
}

enum ring_page_id ring_current_page(const struct ring_state *ring_state) {
  RTT_ASSERT(ring_state);
  return ring_state->page;
}

enum param_id ring_selected_param(const struct ring_state *ring_state) {
  RTT_ASSERT(ring_state);
  return ring_pages[ring_state->page].fields[ring_state->sel_field].id;
}

param_value_t ring_adjust_value(const struct ring_state *ring_state,
                                param_value_t current, int8_t direction) {
  RTT_ASSERT(ring_state);
  RTT_ASSERT(ring_state->page < RING_PAGE_COUNT);
  RTT_ASSERT(ring_state->sel_field < ring_pages[ring_state->page].field_count);

  enum param_id id =
      ring_pages[ring_state->page].fields[ring_state->sel_field].id;

  /* Step the value by calling the stepper function by type */
  return value_steppers[param_desc[id].type](&param_desc[id], current,
                                             direction);
}
