/* -----------------------------------------------------------------------------
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   The voice module manages every aspect of a voice's life-cycle other than
   being allocated a note and passed parameter changes (which the synth does).

   Each voice is effectively a monophonic syntheser, a complete signal chain
   from oscillator to output.  So a polyphonic synth is simply made up of a
   set of monophonic synths coordinated to work together.

   Voices can be in one of three states:

   VOICE_IDLE : Doing nothing and available for a new note.
   VOICE_ACTIVE : Playing a note
   VOICE_STEALING: In the process of switching notes after being stolen.

   A voice is driven like the voice boards of an analogue polysynth, by gate,
   trigger and pitch only:

     voice_trigger()  - gate high plus a trigger (start, retrigger or steal)
     voice_set_note() - pitch change only (legato)
     voice_gate_off() - gate low (release)
     voice_kill()     - immediate silence (panic)

   It need to know nothing about keys, pedals, voice modes or the other voices
   - the keys layer and voice assigner handle these.
  -----------------------------------------------------------------------------
*/

#include <string.h>

#include "dco.h"
#include "trace.h"
#include "voice.h"

extern const float MIDI_FREQ_TABLE[128];

/*
 * scratch buffers for DCO1/DCO2's signals, these are mixed (with DCO2's
 * channel crossfaded to ring mod by DCO_XMOD_LEVEL) into voice->sample_block
 * by dco_mix().
 */
static float dco1_block[AUDIO_BLOCK_SIZE];
static float dco2_block[AUDIO_BLOCK_SIZE];

static void voice_state_idle(struct voice *voice);
static void voice_state_active(struct voice *voice);
static void voice_state_stealing(struct voice *voice);

static void voice_start_chain(struct voice *voice);
static void voice_stop_chain(struct voice *voice);
static void voice_run_chain(struct voice *voice);
static void voice_update_chain(struct voice *voice);
static void voice_init_chain(struct voice *voice);

static void (*voice_state_handlers[])(struct voice *voice) = {
    voice_state_idle, voice_state_active, voice_state_stealing};
/*
 * Initialises the voice
 *
 * Set up the initial state. Passes in sample memory and pointer to the global
 * user parameters.
 *
 * Resets the voice and initialises all the modules in the signal chain.
 */
void voice_init(struct voice *voice, float sample_block[],
                const param_value_t params[]) {
  RTT_ASSERT(voice);
  RTT_ASSERT(sample_block);
  RTT_ASSERT(params);

  voice->sample_block = sample_block;
  voice->params = params;

  /* voice_kill() deliberately leaves this alone, so initialise it here */
  voice->update_pending = false;
  voice->unison_cents = 0.0f;
  voice->start_phase = -1.0f;

  voice_kill(voice);
  voice_init_chain(voice);
}

/*
 * Silence the voice immediately and return it to IDLE (panic, all sound off,
 * voice mode change).  No fade, so it can click - use voice_gate_off() for a
 * normal release.
 *
 * A pending chain update (update_pending) is NOT cleared, it needs to be
 * handled.
 */
void voice_kill(struct voice *voice) {
  RTT_ASSERT(voice);

  voice->note = 0;
  voice->velocity = 0;
  voice->pitch = 0;
  voice->target_note = 0;
  voice->target_velocity = 0;
  voice->target_pitch = 0;
  voice->state = VOICE_IDLE;
  voice->gate = false;

  memset(voice->sample_block, 0, sizeof(float) * AUDIO_BLOCK_SIZE);
}

/*
 * Renders sample data.
 *
 * Call the relevant handler function for the voice state machine.
 *
 * The handlers are function pointers in a jump table so we can directly
 * call them based on the current voice state.
 *
 * This calls one of: voice_state_idle(), voice_state_active() or
 * voice_state_stealing().
 */
void voice_render(struct voice *voice) {
  voice_state_handlers[voice->state](voice);
}

/*
 * Base frequency of a MIDI note.
 */
static float voice_note_pitch(uint8_t midi_note) {
  return MIDI_FREQ_TABLE[midi_note];
}

/*
 * Gate high (on), plus a trigger.
 *
 * MIDI events can arrive at any time during block generation, they are
 * asynchronous and their timing is completely indeterminate.
 *
 * These don't impact the block though because the super-loop ensures that they
 * can only be applied at the next block boundary.
 *
 * The voice render is only ever called from the audio routine and the MIDI
 * routine alwayss follows after and will yield to it.
 *
 * The trigger is handled differently depending on the voice state:
 *
 * - IDLE              : start the note.
 * - ACTIVE, same note : retrigger the envelopes (from their current level).
 * - ACTIVE, new note  : steal - fade the old note out quickly (RTZ), then
 *                       start the new one (see voice_state_stealing()).
 * - STEALING          : switch the note the fade hands over to. Whether a
 *                       mid-steal voice should be retargeted is the voice
 *                       assigner's decision: the poly assigner never does,
 *                       the mono assigner does.
 */
void voice_trigger(struct voice *voice, uint8_t midi_note,
                   uint8_t midi_velocity) {
  RTT_ASSERT(voice);

  voice->gate = true;

  switch (voice->state) {
  case VOICE_IDLE:
#ifdef TRACE_VOICE
    RTT_LOG("%sTrigger: Start %d [voice %d]\n", RTT_CTRL_TEXT_BRIGHT_CYAN,
            midi_note, voice->idx);
#endif
    voice->note = midi_note;
    voice->velocity = midi_velocity;
    voice->pitch = voice_note_pitch(midi_note);

    voice_start_chain(voice);
    env_note_on(&voice->amp_env, voice->note, voice->velocity);
    env_note_on(&voice->mod_env, voice->note, voice->velocity);

    /* The first note of a phrase never glides - only note-to-note legato
     * transitions do (see voice_set_note() below). */
    voice->glide_pitch = voice->pitch;

    voice->state = VOICE_ACTIVE;
    break;

  case VOICE_ACTIVE:
    if (voice->note == midi_note) {
#ifdef TRACE_VOICE
      RTT_LOG("%sTrigger: Retrigger %d [voice %d]\n", RTT_CTRL_TEXT_BRIGHT_CYAN,
              midi_note, voice->idx);
#endif
      voice->velocity = midi_velocity;
      env_note_on(&voice->amp_env, voice->note, voice->velocity);
      env_note_on(&voice->mod_env, voice->note, voice->velocity);
      return;
    }

#ifdef TRACE_VOICE
    RTT_LOG("%sTrigger: Steal (RTZ) %d -> %d [voice %d]\n",
            RTT_CTRL_TEXT_BRIGHT_CYAN, voice->note, midi_note, voice->idx);
#endif
    voice->target_note = midi_note;
    voice->target_velocity = midi_velocity;
    voice->target_pitch = voice_note_pitch(midi_note);
    env_rtz(&voice->amp_env);
    env_rtz(&voice->mod_env);
    voice->state = VOICE_STEALING;
    break;

  case VOICE_STEALING:
#ifdef TRACE_VOICE
    RTT_LOG("%sTrigger: Retarget steal -> %d [voice %d]\n",
            RTT_CTRL_TEXT_BRIGHT_CYAN, midi_note, voice->idx);
#endif
    voice->target_note = midi_note;
    voice->target_velocity = midi_velocity;
    voice->target_pitch = voice_note_pitch(midi_note);
    break;
  }
}

/*
 * Gate off.
 *
 * ACTIVE   : the envelopes go to their release segment.  The signal chain
 *            keeps running so the release tail rings out - see the ENV_OFF
 *            handling in voice_state_active().
 * STEALING : the envelope is in its fast RTZ fade for the old note and must
 *            be left alone (releasing it would replace the fade with the old
 *            note's full release time).  The gate-off belongs to the incoming
 *            note, so it is applied once that note starts.
 * IDLE     : nothing is sounding, nothing to do.
 */
void voice_gate_off(struct voice *voice) {
  RTT_ASSERT(voice);

  voice->gate = false;

  if (voice->state != VOICE_ACTIVE) {
    return;
  }

#ifdef TRACE_VOICE
  RTT_LOG("%sGate off: Release [voice %d]\n", RTT_CTRL_TEXT_BRIGHT_CYAN,
          voice->idx);
#endif
  env_note_off(&voice->amp_env);
  env_note_off(&voice->mod_env);
}

/*
 * Pitch change without a trigger - mono legato.  The envelopes carry on.
 *
 * ACTIVE   : the note changes now (or becomes the glide target when
 *            portamento is on).
 * STEALING : the note the fade hands over to changes, so the voice starts on
 *            the key actually held once the fade ends.
 * IDLE     : nothing is sounding, nothing to do.
 */
void voice_set_note(struct voice *voice, uint8_t midi_note) {
  RTT_ASSERT(voice);

  if (voice->state == VOICE_STEALING) {
    voice->target_note = midi_note;
    voice->target_pitch = voice_note_pitch(midi_note);
    return;
  }

  if (voice->state == VOICE_IDLE) {
    return;
  }

  voice->note = midi_note;
  voice->pitch = voice_note_pitch(midi_note);

  /*
   * With portamento on, voice->pitch becomes the glide target
   */
  if (!voice->portamento_enable) {
    dco_note_on(&voice->dco_1, voice->pitch, true);
    dco_note_on(&voice->dco_2, voice->pitch, true);
  }

#ifdef TRACE_VOICE
  RTT_LOG("voice_set_note %d [voice %d]\n", midi_note, voice->idx);
#endif
}

/*
 * Requests a signal chain parameter update.
 *
 * Updating the chain refreshes every module's parameters and also
 * rebuilds the mod matrix so is deferred. This ensures that bursts of CC
 * changes arriving before the next render collapses into a single update
 * instead of one per parameter message.
 */
void voice_update(struct voice *voice) { voice->update_pending = true; }

/*
 * Handles the VOICE_IDLE state
 *
 * The only thing that needs servicing while idle is a pending chain
 * update.
 */
static void voice_state_idle(struct voice *voice) {
  if (voice->update_pending) {
#ifdef TRACE_VOICE
    RTT_LOG("%sRender: Update chain (IDLE) [voice %d]\n",
            RTT_CTRL_TEXT_BRIGHT_GREEN, voice->idx);
#endif
    voice_update_chain(voice);
    voice->update_pending = false;
  }
}

/*
 * Handles the VOICE_ACTIVE state
 *
 * All we service while active is a deferred update or the envelope
 * reaching OFF.
 *
 * ENV_OFF: The release (or an immediate zero-sustain note) has finished -
 * silence is confirmed, so stop the signal chain and return to IDLE.
 */
static void voice_state_active(struct voice *voice) {
  if (voice->update_pending) {
#ifdef TRACE_VOICE
    RTT_LOG("%sRender: Update chain [voice %d]\n", RTT_CTRL_TEXT_BRIGHT_GREEN,
            voice->idx);
#endif

    voice_update_chain(voice);
    voice->update_pending = false;
  }

  if (voice->amp_env.state == ENV_OFF) {
#ifdef TRACE_VOICE
    RTT_LOG("%sRender: Service ENV_OFF [voice %d]\n",
            RTT_CTRL_TEXT_BRIGHT_GREEN, voice->idx);
#endif
    /*
     * Envelope has ended so note is now silent, stop the signal chain running
     * and set the voice to IDLE
     */
    voice_stop_chain(voice);
    voice->state = VOICE_IDLE;
    voice->gate = false;

    /*
     * The DCA fades out rather than snapping straigh to zero.  This block
     * won't go to idle itself as nothing touches sample_block again while
     * idle.  So we clear the samples otherwise the (very quiet) tail
     * ends up mixed in on every subsequent block until the voice is reused.
     */
    memset(voice->sample_block, 0, sizeof(float) * AUDIO_BLOCK_SIZE);

    /* We return directly here as there is no need to run the signal chain,
    the voice is silent */
    return;
  }

  /* Run the signal chain to generate samples */
  voice_run_chain(voice);
}

/*
 * Service the voice steal
 *
 * We steal the voice in 2 distinct phases:
 *
 * Phase 1. voice_trigger() fades the current note quickly to zero (RTZ).
 * Phase 2. Once silent (ENV_OFF) we swap the steal note in and reset the voice
 * to VOICE_ACTIVE.
 *
 * This 2-stage approach stops the note from abruptly changing mid-envelope
 * which would cause audible artifacts. The special state VOICE_STEALING is
 * used to inform the synth voice allocator that the voice is mid-steal and
 * cannot be stolen again until the transition is complete.
 *
 * ENV_OFF: RTZ has silenced the voice - swap in the new note and start it,
 * same block.
 */
static void voice_state_stealing(struct voice *voice) {
  if (voice->update_pending) {
    voice_update_chain(voice);
    voice->update_pending = false;
  }

  if (voice->amp_env.state == ENV_OFF) {
#ifdef TRACE_VOICE
    RTT_LOG("%sRender: Service ENV_OFF(RTZ) [voice %d]\n",
            RTT_CTRL_TEXT_BRIGHT_GREEN, voice->idx);
#endif
    voice->note = voice->target_note;
    voice->velocity = voice->target_velocity;
    voice->pitch = voice->target_pitch;

    /* Leave the signal chain running - we're going straight into a steal */
    voice_start_chain(voice);
    env_note_on(&voice->amp_env, voice->note, voice->velocity);
    env_note_on(&voice->mod_env, voice->note, voice->velocity);
    voice->glide_pitch = voice->pitch;

    voice->state = VOICE_ACTIVE;
  }

  /* Run the signal chain to generate samples */
  voice_run_chain(voice);

  /*
   * The incoming note's gate went low while we were still fading the old
   * note out, so release it now rather than leaving it stuck ringing with no
   * key down and no gate-off coming.
   *
   * This must come after the chain has run.  The new note's envelope is
   * still at level 0 until its first attack step, and a release at level 0
   * goes straight to ENV_OFF - the note would be dropped without sounding.
   * After one block it has some level and releases normally.
   */
  if (voice->state == VOICE_ACTIVE && !voice->gate) {
    voice_gate_off(voice);
  }
}

/*
 * Start the signal chain
 *
 * Not all modules are started with a note_on, only those that
 * need to know the note or velocity. The rest run continuously.
 *
 * The LFO is a special case, it will always be free running
 * however the note_on causes it to reset if the LFO is set to
 * NOTE mode.  It has no note_off.
 */
void voice_start_chain(struct voice *voice) {
#ifdef TRACE_VOICE
  RTT_LOG("%sStart voice chain %d\n", RTT_CTRL_TEXT_WHITE, voice->idx);
#endif
  dco_note_on(&voice->dco_1, voice->pitch, false);
  dco_note_on(&voice->dco_2, voice->pitch, false);

  /*
   * Unison start phase (set by the mono assigner).  Applied here, when a note
   * actually starts - including after a steal - and so never to a voice that
   * is still fading out its previous note.
   */
  if (voice->start_phase >= 0.0f) {
    dco_set_phase(&voice->dco_1, voice->start_phase);
    dco_set_phase(&voice->dco_2, voice->start_phase);
  }

  noise_note_on(&voice->noise);
  lfo_note_on(&voice->lfo1);
  dcf_note_on(&voice->dcf, voice->note);
  dca_note_on(&voice->dca);
}

/*
 * Stops the signal chain
 *
 * Only modules (other than LFO) with a corresponding note_on get this.
 */
void voice_stop_chain(struct voice *voice) {
#ifdef TRACE_VOICE
  RTT_LOG("%sStop voice chain %d\n", RTT_CTRL_TEXT_WHITE, voice->idx);
#endif
  dco_note_off(&voice->dco_1);
  dco_note_off(&voice->dco_2);
  noise_note_off(&voice->noise);
}

/*
 * Run the signal chain
 *
 * The order is significant as we want the modulation sources run before
 * their targets.
 */

void voice_run_chain(struct voice *voice) {
#ifdef TRACE_VOICE
  DWT_INIT();
#endif

  /*
   * Mono-mode legato glide: slide the DCO pitch toward voice->pitch on
   * each block.
   */
  if (voice->portamento_enable) {
    voice->glide_pitch +=
        (voice->pitch - voice->glide_pitch) * voice->portamento_coeff;
    dco_note_on(&voice->dco_1, voice->glide_pitch, true);
    dco_note_on(&voice->dco_2, voice->glide_pitch, true);
  }

#ifdef TRACE_VOICE
  DWT_CLEAR();
#endif
  lfo_render(&voice->lfo1, &voice->mod_sources[LFO1_TRI],
             &voice->mod_sources[LFO1_SAW], &voice->mod_sources[LFO1_RAMP],
             &voice->mod_sources[LFO1_SQUARE], &voice->mod_sources[LFO1_SANDH]);

  env_render(&voice->mod_env, &voice->mod_sources[ENV1_LEVEL]);
  env_render(&voice->amp_env, &voice->dca_env_level);

  /*
   * Resolve the modulation slots: sums each slot's (source * depth) into its
   * routed destination, ready for the modules below to read.
   */
  mod_matrix_render(voice->slots, MAX_MOD_SLOTS, voice->mod_sources,
                    voice->mod_dest);

  /*
   * The mod wheel has 3 fixed destinations, DCO, DCO+DCF, DCF. Typically
   * it just increases any existing modulation depth
   */
  float mod_wheel_scale = 1.0f + voice->mod_wheel * MOD_WHEEL_DEPTH_MAX;
  enum mod_wheel_dest wheel_dest = voice->params[SYNTH_MOD_WHEEL_DEST].e;

  if (wheel_dest == MOD_WHEEL_DEST_VCO ||
      wheel_dest == MOD_WHEEL_DEST_VCO_VCF) {
    voice->mod_dest[MOD_DEST_DCO1_PITCH] *= mod_wheel_scale;
    voice->mod_dest[MOD_DEST_DCO2_PITCH] *= mod_wheel_scale;
  }

  if (wheel_dest == MOD_WHEEL_DEST_VCF ||
      wheel_dest == MOD_WHEEL_DEST_VCO_VCF) {
    voice->mod_dest[MOD_DEST_DCF_CUTOFF] *= mod_wheel_scale;
  }
#ifdef TRACE_VOICE
  DWT_OUTPUT("voice:modulators (LFO+ENVx2+matrix)");
#endif

#ifdef TRACE_VOICE
  DWT_CLEAR();
#endif
  dco_render(&voice->dco_1, dco1_block, voice->mod_dest[MOD_DEST_DCO1_PITCH],
             voice->mod_dest[MOD_DEST_DCO1_PWM]);
  dco_render(&voice->dco_2, dco2_block, voice->mod_dest[MOD_DEST_DCO2_PITCH],
             voice->mod_dest[MOD_DEST_DCO2_PWM]);

  /*
   * Mix the DCOs at their level settings.  DCO_XMOD_LEVEL crossfades DCO2's
   * mixer channel from DCO2 to the DCO1 x DCO2 ring product - see dco_mix().
   */
  dco_mix(voice->sample_block, dco1_block, dco2_block, &voice->dco_1,
          &voice->dco_2, voice->params[DCO_RINGMOD_LEVEL].f);

  noise_render(&voice->noise, voice->sample_block);
#ifdef TRACE_VOICE
  DWT_OUTPUT("voice: DCO 1+2");
#endif

#ifdef TRACE_VOICE
  DWT_CLEAR();
#endif
  shape_render(&voice->shaper, voice->sample_block);
#ifdef TRACE_VOICE
  DWT_OUTPUT("voice: waveshaper");
#endif

#ifdef TRACE_VOICE
  DWT_CLEAR();
#endif
  dcf_render(&voice->dcf, voice->sample_block,
             voice->mod_dest[MOD_DEST_DCF_CUTOFF]);
#ifdef TRACE_VOICE
  DWT_OUTPUT("voice: DCF");
#endif

#ifdef TRACE_VOICE
  DWT_CLEAR();
#endif
  dca_render(&voice->dca, voice->sample_block, voice->dca_env_level,
             voice->mod_dest[MOD_DEST_DCA_LEVEL]);
#ifdef TRACE_VOICE
  DWT_OUTPUT("voice: DCA");
#endif
}

/*
 * Initialise the signal chain.
 */
void voice_init_chain(struct voice *voice) {
  env_init(&voice->amp_env);
  env_init(&voice->mod_env);

  dco_init(&voice->dco_1, 0.33f);
  dco_init(&voice->dco_2, 0.33f);
  noise_init(&voice->noise, 0.33f);
  shape_init(&voice->shaper);

  dca_init(&voice->dca);
  dcf_init(&voice->dcf);

  lfo_init(&voice->lfo1);
}

/*
 * Update the signal chain modules.
 *
 * Frugi doesn't check which parameters might have changed, the whole set is
 * small enough that we can just refresh them all on each block, not worth
 * the overhead of 'is_dirty' type checks.
 */
void voice_update_chain(struct voice *voice) {
#ifdef TRACE_VOICE
  DWT_INIT();
  DWT_CLEAR();
#endif

  dca_update(&voice->dca, voice->params[DCA_LEVEL].f);

  env_update(
      &voice->amp_env, voice->params[DCA_ATTACK].f, voice->params[DCA_DECAY].f,
      voice->params[DCA_SUSTAIN].f, voice->params[DCA_RELEASE].f, ENV_NORMAL,
      voice->params[DCA_NOTE_TRACK].b, voice->params[DCA_VELOCITY_TRACK].b);

  env_update(&voice->mod_env, voice->params[ENV1_ATTACK].f,
             voice->params[ENV1_DECAY].f, voice->params[ENV1_SUSTAIN].f,
             voice->params[ENV1_RELEASE].f, voice->params[ENV1_MODE].e, false,
             false);

  dco_update(&voice->dco_1, voice->params[DCO1_LEVEL].f,
             voice->params[DCO1_WAVE].e, voice->params[DCO1_OCTAVE].i,
             voice->params[DCO1_SEMI].i, voice->params[DCO1_CENTS].f,
             voice->params[DCO1_PULSEWIDTH].f);

  /* Unison detune is applied with the bend, so it is heard on a held note */
  const float pitch_offset =
      voice->pitch_bend_semitones + voice->unison_cents / 100.0f;
  dco_set_bend(&voice->dco_1, pitch_offset);

  noise_update(&voice->noise, voice->params[NOISE_LEVEL].f);

  shape_update(&voice->shaper, voice->params[DCO_DRIVE].f);

  dco_update(&voice->dco_2, voice->params[DCO2_LEVEL].f,
             voice->params[DCO2_WAVE].e, voice->params[DCO2_OCTAVE].i,
             voice->params[DCO2_SEMI].i, voice->params[DCO2_CENTS].f,
             voice->params[DCO2_PULSEWIDTH].f);
  dco_set_bend(&voice->dco_2, pitch_offset);

  dcf_update(&voice->dcf, voice->params[DCF_MODE].e,
             voice->params[DCF_CUTOFF].f, voice->params[DCF_RES].f,
             voice->params[DCF_SAT].f, voice->params[DCF_TRACK].b);

  lfo_update(&voice->lfo1, voice->params[LFO1_RATE].f,
             voice->params[LFO1_MODE].e);

  /*
   * Fixed routing table
   */
  voice->slots[0] =
      (struct mod_slot){voice->params[DCO1_MOD_SOURCE].e, MOD_DEST_DCO1_PITCH,
                        voice->params[DCO1_MOD_DEPTH].f};
  voice->slots[1] =
      (struct mod_slot){voice->params[DCO1_PWM_SOURCE].e, MOD_DEST_DCO1_PWM,
                        voice->params[DCO1_PWM_DEPTH].f};
  voice->slots[2] =
      (struct mod_slot){voice->params[DCO2_MOD_SOURCE].e, MOD_DEST_DCO2_PITCH,
                        voice->params[DCO2_MOD_DEPTH].f};
  voice->slots[3] =
      (struct mod_slot){voice->params[DCO2_PWM_SOURCE].e, MOD_DEST_DCO2_PWM,
                        voice->params[DCO2_PWM_DEPTH].f};
  voice->slots[4] =
      (struct mod_slot){voice->params[DCF_MOD_SOURCE].e, MOD_DEST_DCF_CUTOFF,
                        voice->params[DCF_MOD_DEPTH].f};
  voice->slots[5] =
      (struct mod_slot){voice->params[DCA_MOD_SOURCE].e, MOD_DEST_DCA_LEVEL,
                        voice->params[DCA_MOD_DEPTH].f};
#ifdef TRACE_VOICE
  DWT_OUTPUT("voice_update_chain cost");
#endif
}
