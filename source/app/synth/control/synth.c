/* -----------------------------------------------------------------------------
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   The synth is the top-level module for the instrument owning the voices and
   managing global performance parameters.

   - Dispatches MIDI messages.  Notes and pedals go to the keys layer
     (keys.c), which reports key and gate events to the voice assigner for
     the current voice mode (assign_poly.c / assign_mono.c) - see
     design-voice-architecture.md.
   - Loading/saving of patches and passing updated parameters voices.
   - Sums (mixes) the output of all the voices into a stereo pair.

   All these actions are carried out at the start of each audio block when a
   callback is received from the audio engine.
  -----------------------------------------------------------------------------
   Compile time directives:

   These can be overridden in the CMakeLists.txt build file.

   SYNTH_MAX_VOICES: Sets max voices to 6 or 8 (6-Debug, 8-Release)
   MIX_SOFT_CLIP: Enables the soft-knee summing mix output clipper (OFF)

   Tracing is through Segger RTT so only visible with a J-Link probe. These
   are broad categories and logging will be the state I last left it in.

   TRACE_SYNTH: Enables trace output (OFF)
   TRACE_LEVELS: Enables summing mixer levels/overshoot logging (OFF)
   TRACE_MIDI: Traces incoming MIDI messages and dispatch (OFF)
   TRACE_KEYS / TRACE_ASSIGN / TRACE_VOICE: see keys.c, assign_*.c, voice.c
   -----------------------------------------------------------------------------
*/
#include <string.h>

#include "map.h"
#include "midi_parser.h"
#include "param.h"
#include "patch.h"
#include "synth.h"
#include "trace.h"

static void synth_update_voice_params(struct synth *synth);
static void synth_update_performance_params(struct synth *synth);
static bool synth_channel_mode(struct synth *synth, uint8_t cc);
static void synth_panic(struct synth *synth);
static inline float soft_clip(float x);

/*
 * Keys layer events are passed straight to the assigner for the current
 * voice mode.  ctx is the synth (registered in synth_init()).
 */
static void on_key_down(void *ctx, uint8_t note, uint8_t velocity) {
  struct synth *synth = ctx;
  synth->assigner->key_down(synth, note, velocity);
}

static void on_key_up(void *ctx, uint8_t note) {
  struct synth *synth = ctx;
  synth->assigner->key_up(synth, note);
}

static void on_gate_off(void *ctx, uint8_t note) {
  struct synth *synth = ctx;
  synth->assigner->gate_off(synth, note);
}

static const struct keys_events keys_events = {
    .key_down = on_key_down, .key_up = on_key_up, .gate_off = on_gate_off};

/*
 * Initialize synth, allocate sample memory to voices, load the default
 * patch. Each voice receives a pointer to its slice of the pre-allocated
 * sample buffer.
 *
 * patch_load resolves a user patch slot for MIDI Program Change (see
 * synth_handle_midi()) - pass NULL if not wired up.
 */
void synth_init(struct synth *synth, const uint8_t midi_channel,
                synth_patch_load_fn patch_load) {
  RTT_ASSERT(synth);

  synth->midi_channel = midi_channel;
  synth->patch_load = patch_load;

  /*
   * The live performance controller state is not part the patch so only
   * initialised at start up.
   */
  synth->mod_wheel = 0.0f;
  synth->pitch_bend_semitones = 0.0f;
  synth->chorus_enable = false;

  /* Voice management: no assigner chosen yet */
  keys_init(&synth->keys, &keys_events, synth);

  synth->assigner = nullptr;
  synth->trigger_counter = 0;
  memset(synth->trigger_seq, 0, sizeof(synth->trigger_seq));
  synth->mono_note = KEYS_NONE;
  synth->unison_in_phase = true;

  param_midi_map_init();
  param_create_default_patch(synth->params);

  /*
   * Allocate a sample buffer (owned by synth) and a pointer to the params to
   * each voice.  Then call a first voice update to apply the default patch.
   */
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    synth->voice[i].idx = i;
    voice_init(&synth->voice[i], synth->sample_blocks + (i * AUDIO_BLOCK_SIZE),
               synth->params);
    voice_update(&synth->voice[i]);
  }

  /*
   * Player performance parameters - this also chooses the assigner for the
   * patch's voice mode, so it comes after the voices exist.
   */
  synth_update_performance_params(synth);

  chorus_init(&synth->chorus);

  /* Set parameters as initially changed, UI etc responds to this */
  set_param_changed(true);

  // RTT_LOG("%sSynth initialised. MIDI channel %d\n", RTT_CTRL_TEXT_WHITE,
  //         synth->midi_channel);
}

/*
 * This is a cubic soft-clip / soft-knee saturator.  It is used to tame any
 * wild resonance peaks coming from the summed voices.  Theoretically there
 * should not be any, practically I've always found it hard to ensure this,
 * gain staging is tricky.
 *
 * This adds a performance cost when summing the voices so only compiled in
 * if MIX_SOFT_CLIP is defined at compile time.
 */
#ifdef MIX_SOFT_CLIP
static inline float soft_clip(float x) {
  x = fmaxf(fminf(x, 1.0f), -1.0f);
  return 1.5f * x - 0.5f * x * x * x;
}
#endif

/*
 *
 * This is the voice summing mixer, it populates the stereo audio buffer which
 * the audio engine passes to the audio hardware subsystem.
 *
 * This has been hand-optimised.  It looks a little clunky since individual
 * pointers are used for tracking through each voice but DWT measurements show
 * that it reduces the cost of a render from 360 to 211 cycles for each voice.
 *
 * The unrolled voice handling below is tied directly to SYNTH_MAX_VOICES. Only
 * 6 and 8 voices are currently supported, an error is thrown if configured
 * any other way.
 */
void synth_render(struct synth *synth, float *left, float *right) {
  RTT_ASSERT(synth);
  RTT_ASSERT(left);
  RTT_ASSERT(right);

#ifdef TRACE_SYNTH
  DWT_INIT();
  DWT_CLEAR();
#endif

#pragma clang loop unroll_count(8)
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    voice_render(&synth->voice[i]);
  }

#ifdef TRACE_SYNTH
  DWT_OUTPUT("synth: voice render (all voices)");
  uint32_t trace_voice_us = dwt_time_us;
  DWT_CLEAR();
#endif

  const float scale = synth->assigner->mix_scale(synth);

  float *restrict lp = left;
  float *restrict end = lp + AUDIO_BLOCK_SIZE;
  float *restrict rp = right;

  /* Point to each voice's sample buffer */
  const float *restrict v0p = synth->voice[0].sample_block;
  const float *restrict v1p = synth->voice[1].sample_block;
  const float *restrict v2p = synth->voice[2].sample_block;
  const float *restrict v3p = synth->voice[3].sample_block;
  const float *restrict v4p = synth->voice[4].sample_block;
  const float *restrict v5p = synth->voice[5].sample_block;
#if SYNTH_MAX_VOICES == 8
  const float *restrict v6p = synth->voice[6].sample_block;
  const float *restrict v7p = synth->voice[7].sample_block;
#elif SYNTH_MAX_VOICES != 6
#error "synth_render unrolling only handles SYNTH_MAX_VOICES == 6 or 8"
#endif

#ifdef TRACE_LEVELS
  // DWT_INIT()
  // DWT_CLEAR();
#endif

  while (lp < end) {
#if SYNTH_MAX_VOICES == 8
    float raw = (*v0p + *v1p + *v2p + *v3p + *v4p + *v5p + *v6p + *v7p) * scale;
#else
    float raw = (*v0p + *v1p + *v2p + *v3p + *v4p + *v5p) * scale;
#endif

#ifdef TRACE_LEVELS
    if (fabsf(raw) > 1.0f) {
      RTT_RED();
      RTT_LOG_FLOAT("mix overshoot:", raw);
      RTT_WHITE();
    }
#endif

#ifdef MIX_SOFT_CLIP
    /* Catch any resonant peaks etc that get through voice scaling*/
    *lp = soft_clip(raw);
#else
    *lp = raw;
#endif

    *rp = *lp; /* Voic is MONO just copy L->R channel */

    lp++;
    rp++;
    v0p++;
    v1p++;
    v2p++;
    v3p++;
    v4p++;
    v5p++;
#if SYNTH_MAX_VOICES == 8
    v6p++;
    v7p++;
#endif
  }

  if (synth->chorus_enable) {
    chorus_render(&synth->chorus, left, right);
  }

#ifdef TRACE_LEVELS
  // DWT_OUTPUT("soft_clip")
#endif

#ifdef TRACE_SYNTH
  DWT_OUTPUT("synth: mix");
  uint32_t trace_total_us = trace_voice_us + dwt_time_us;
  uint32_t trace_budget_us = (AUDIO_BLOCK_SIZE * 1000000UL) / AUDIO_SAMPLE_RATE;
  uint32_t trace_pct_used = (trace_total_us * 100UL) / trace_budget_us;

  if (trace_total_us <= trace_budget_us) {
    RTT_LOG(
        "synth: block %lu us / budget %lu us (%lu%% used, %lu us headroom)\n",
        (unsigned long)trace_total_us, (unsigned long)trace_budget_us,
        (unsigned long)trace_pct_used,
        (unsigned long)(trace_budget_us - trace_total_us));
  } else {
    RTT_RED();
    RTT_LOG("synth: block %lu us / budget %lu us (%lu%% used, OVER BUDGET by "
            "%lu us)\n",
            (unsigned long)trace_total_us, (unsigned long)trace_budget_us,
            (unsigned long)trace_pct_used,
            (unsigned long)(trace_total_us - trace_budget_us));
    RTT_WHITE();
  }
#endif
}

/*
 * Dispatch MIDI messages to appropriate handlers.
 *
 * All MIDI messages arrive pre-validated from the parser.
 * CC messages are mapped to voice parameters or synth parameters. Synth params
 * are global and not part of a patch so must be handled directly.
 */
void synth_handle_midi(struct synth *synth, uint8_t status, uint8_t data1,
                       uint8_t data2) {
  RTT_ASSERT(synth);

#ifdef TRACE_MIDI
  RTT_LOG("MIDI: %x, %x, %x\n", status, data1, data2);
#endif

  /*
   * Channel messages
   */
  uint8_t status_type = (status < 0xF0) ? (status & 0xF0) : status;

  switch (status_type) {
  /*
   * Notes and pedals go to the keys layer, which decides when each note's
   * gate falls and passes key and gate events on to the assigner.
   */
  case MIDI_STATUS_NOTE_ON:
    if (data2 > 0) {
      keys_key_down(&synth->keys, data1, data2);
    } else {
      /*
       * Some devices send a NOTE_ON with a velocity of 0 instead of a
       * NOTE_OFF
       */
      keys_key_up(&synth->keys, data1);
    }
    break;

  case MIDI_STATUS_NOTE_OFF:
    keys_key_up(&synth->keys, data1);
    break;

  case MIDI_STATUS_CONTROL_CHANGE: {
    /* CC120-127 are channel mode messages, never patch parameters */
    if (synth_channel_mode(synth, data1)) {
      return;
    }

    /*
     * Sustain, sostenuto pedal, mod wheel and the soft takeover toggle are
     * live performance controller state, handled directly here rather than
     * through the params[] array
     */
    if (data1 == MIDI_CC_HOLDPEDAL) {
      keys_sustain(&synth->keys, data2 >= 64);
      return;
    }

    if (data1 == MIDI_CC_SOSTENUTO) {
      keys_sostenuto(&synth->keys, data2 >= 64);
      return;
    }

    if (data1 == MIDI_CC_MODULATIONWHEEL) {
      synth->mod_wheel = param_cc7bit_to_uni(data2);
      synth_update_voice_params(synth);
      return;
    }

    if (data1 == MIDI_CC_SOFT_TAKEOVER) {
      param_set_soft_takeover(data2 >= 64);
      return;
    }

    /*
     * MAP the CC message to a paremeter and store the value, this tells us the
     * type of message (VOICE or SYNTH).
     */
    enum param_mapping_type type =
        param_set_value_from_cc(data1, data2, synth->params);

    if (type == PARAM_VOICE) {
      synth_update_voice_params(synth);
    } else if (type == PARAM_SYNTH) {
      synth_update_performance_params(synth);
    } else if (type == PARAM_UNMAPPED) {
#ifdef TRACE_MIDI
      RTT_LOG("Unmapped CC message %d\n", data1);
#endif
    }
    /*
     * else PARAM_PENDING: soft takeover is holding this update until the
     * control's value catches up with the parameter's current position.
     */
    break;
  }

  /*
   * Pitch bend is different from the other MIDI messages, it sends a finer
   * resolution value than the other CC messages.  It uses 2 MIDI bytes to do
   * this, passing the lo and hi bytes of a 14-bit value. 8192 represents the
   * 0 (no bend) value, with lower or higher than this point being bend down
   * or up respectively.
   */
  case MIDI_STATUS_PITCH_BEND: {

    /* data1 = LSB, data2 = MSB, center = 8192 */
    int16_t raw = (int16_t)(((uint16_t)data2 << 7) | data1) - 8192;
    float normalized = fmaxf(-1.0f, fminf(1.0f, raw / 8192.0f));
    float range = param_uni_to_linear(synth->params[SYNTH_BEND_RANGE].f, 0,
                                      BEND_RANGE_MAX);

#ifdef TRACE_MIDI
    RTT_LOG("Bend %d\n", raw);
#endif

    synth->pitch_bend_semitones = normalized * range;
    synth_update_voice_params(synth);
    break;
  }

  /*
   * Program change is used to change patch.  This is often a bank # and
   * patch # however Frugi only has a bank #0 so we only use the patch
   * number, taken directly as a user patch slot (0-127, which happens to
   * be exactly the slot count - see patch_persist.h). An empty or
   * unresolvable slot (or no patch_load wired up at all) falls back to
   * the default patch rather than being ignored.
   */
  case MIDI_STATUS_PROGRAM_CHANGE: {
    param_value_t loaded[PARAM_COUNT];

    if (synth->patch_load == NULL ||
        !synth->patch_load((uint16_t)data1, loaded, NULL)) {
      param_create_default_patch(loaded);
    }

    synth_load_patch(synth, loaded);
    break;
  }

  /*
   * System Reset (0xFF, a realtime byte) - return to the power-up state, as
   * far as playing is concerned: everything silent and every controller back
   * at rest.  The patch is left alone.
   */
  case MIDI_STATUS_SYS_RESET:
    synth_panic(synth);
    break;
  }
}

/*
 * Hard stop: silence every voice at once and forget every key and pedal, then
 * put the live controllers back at rest.
 */
static void synth_panic(struct synth *synth) {
  synth->assigner->all_off(synth);
  keys_reset(&synth->keys);

  synth->pitch_bend_semitones = 0.0f;
  synth->mod_wheel = 0.0f;
  synth_update_voice_params(synth);
}

/*
 * Channel mode messages, CC120-127 (design decision D2).  Returns false for
 * any other CC.
 *
 * CC120 All Sound Off   - the hard kill: voices cut immediately, every key
 *                         and pedal forgotten (no release phase).
 * CC121 Reset All Ctrls - pedals lifted (releasing what they held), pitch
 *                         bend and mod wheel centred.  Keys still down keep
 *                         sounding.
 * CC122 Local Control   - ignored, there is no local keyboard.
 * CC123 All Notes Off   - every key comes up as if released by hand, so the
 *                         notes release normally and the pedals still apply.
 * CC124-127 Omni Off/On, Mono On, Poly On - the MIDI spec says each of these
 *                         also acts as All Notes Off.  Omni can't be
 *                         switched over MIDI - the receive channel is a
 *                         build/panel setting.
 *                         Mono and Poly On set the voice mode, as if it had
 *                         been changed on the panel.
 *
 * Panic buttons on controllers and DAWs usually send CC120 and/or CC123 on
 * every channel; CC120 is the one that silences a sustained note at once.
 */
static bool synth_channel_mode(struct synth *synth, uint8_t cc) {
  switch (cc) {
  case MIDI_CC_ALLSOUNDOFF:
    synth->assigner->all_off(synth);
    keys_reset(&synth->keys);
    return true;

  case MIDI_CC_RESETALLCONTROLLERS:
    keys_release_pedals(&synth->keys);
    synth->pitch_bend_semitones = 0.0f;
    synth->mod_wheel = 0.0f;
    synth_update_voice_params(synth);
    return true;

  case MIDI_CC_LOCALCONTROL:
    return true;

  case MIDI_CC_ALLNOTESOFF:
  case MIDI_CC_OMNIMODEOFF:
  case MIDI_CC_OMNIMODEON:
    keys_all_keys_up(&synth->keys);
    return true;

  case MIDI_CC_MONOMODEON:
  case MIDI_CC_POLYMODEON:
    keys_all_keys_up(&synth->keys);
    synth->params[SYNTH_VOICE_MODE].e =
        (cc == MIDI_CC_MONOMODEON) ? VOICE_MODE_MONO : VOICE_MODE_POLY;
    synth_update_performance_params(synth);
    return true;

  default:
    return false;
  }
}

/*
 * This is used by a UI to set parameters.  It is an alternative entry point
 * to the dispatcher.
 *
 * The UI values are routed through the same mechanism as any other MIDI CC
 * message so essentially the UI is just a 'built-in' MIDI controller.
 *
 * While this is a simple solution, it lacks finesse as the UI would typically
 * offer a finer resolution than MIDI which is not the case here.
 */
void synth_set_param(struct synth *synth, uint8_t param_id,
                     param_value_t value) {
  RTT_ASSERT(synth);
  RTT_ASSERT(param_id < PARAM_COUNT);

  synth->params[param_id] = value;

  if (param_id < VOICE_PARAM_COUNT) {
    synth_update_voice_params(synth);
  } else {
    synth_update_performance_params(synth);
  }
}

/*
 * Installs an already-resolved patch wholesale - used above for MIDI
 * Program Change, and by the UI's patch page once it has resolved a
 * user slot to parameters itself (patch_persist_load() isn't reachable
 * from here - see synth_patch_load_fn in synth.h for why).
 */
void synth_load_patch(struct synth *synth,
                      const param_value_t params[PARAM_COUNT]) {
  RTT_ASSERT(synth);
  RTT_ASSERT(params);

  memcpy(synth->params, params, sizeof(synth->params));
  synth_update_voice_params(synth);
  synth_update_performance_params(synth);
}

/*
 * Update the voice parameters.
 *
 * Call each voice to update their internal state and those of the signal
 * chain directly. The voice manages voice parameter updates entirely.
 */
void synth_update_voice_params(struct synth *synth) {
  #pragma clang loop unroll_count(8)
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    synth->voice[i].pitch_bend_semitones = synth->pitch_bend_semitones;
    synth->voice[i].mod_wheel = synth->mod_wheel;
    voice_update(&synth->voice[i]);
  }

  /* Let UI know, it will reset the state */
  set_param_changed(true);
}

/*
 * Update performance parameters.
 *
 * These are player controls such as pitch-bend, mod-wheel and sustain.
 */
static void synth_update_performance_params(struct synth *synth) {
  /*
   * Voice mode selects the assigner strategy - the one place voice mode is
   * tested.  A change silences the voices (to prevent stuck notes); the keys
   * layer keeps its state, so keys still held are released normally later
   * (design decision D6).
   */
  synth->mode = synth->params[SYNTH_VOICE_MODE].e;
  const struct assigner *wanted =
      (synth->mode == VOICE_MODE_MONO) ? &mono_assigner : &poly_assigner;

  if (wanted != synth->assigner) {
    if (synth->assigner) {
      synth->assigner->all_off(synth);
    }
    synth->assigner = wanted;
  }

  /* Per voice performance controls */
  synth->portamento_enable = synth->params[SYNTH_PORTAMENTO_ON].b;
  synth->portamento_time = param_uni_to_linear(
      synth->params[SYNTH_PORTAMENTO_TIME].f, 0, PORTAMENTO_TIME_MAX);
  /* Squared, so most of the control's travel covers the useful 0-40 cents */
  synth->unison_detune = param_uni_to_quadratic(
      synth->params[SYNTH_UNISON_DETUNE].f, 0, UNISON_DETUNE_MAX);

  /* Stereo FX controls */
  /* Clear the delay line when the chorus is switched on - see chorus_reset() */
  bool chorus_was_on = synth->chorus_enable;
  synth->chorus_enable = synth->params[SYNTH_CHORUS_ON].b;

  if (synth->chorus_enable && !chorus_was_on) {
    chorus_reset(&synth->chorus);
  }

  /*
   * Portamento glides from one note to another.  We calculate a simple
   * constant coefficient to apply on each block. This is the same for all
   * voices so can be calculated globally here.
   */
  float glide_blocks = fmaxf(
      synth->portamento_time * (AUDIO_SAMPLE_RATE / 1000.0f) / AUDIO_BLOCK_SIZE,
      1.0f);
  float portamento_coeff = 1.0f - expf(-1.0f / glide_blocks);

  /*
   * Broadcast performance state to each voice.
   */
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    synth->voice[i].portamento_enable = synth->portamento_enable;
    synth->voice[i].portamento_coeff = portamento_coeff;
  }

  /* Per-voice settings owned by the strategy - unison detune and phases */
  synth->assigner->update(synth);

  /* Let the UI etc know*/
  set_param_changed(true);
}
