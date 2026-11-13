/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   A block-based envelope generator.  This uses Nigel Redmon's algorithm for
   creating analogue style segments.

   The block based EG causes stepping in the envelope.  This is not musically
   relevant for most modulation cases however I've elected to add smoothing to 
   the DCA (final output) to compensate for any stepping artefacts that might
   be audible.  Sample accurate envelopes are too much for the limited compute
   resources of the STM32F4.

   The EG is a state machine and responsible for voice lifecycle.  A note-off
   received by a voice does not mean that the note actually ends, the envelope
   will decide this depending on any release time segment.  The note only truly
   ends when ENV_OFF state is reached.

   The envelope provides biased and inverted outputs.  

   Attack and decay can be scaled by velocity/pitch tracking, it is often
   assumed that volume is all that needs to be scaled for this purpose however 
   the shape of the note is equally as important.  Most vintage synths do not
   scale level at all, only AD.
   -----------------------------------------------------------------------------
*/

#include "env.h"
#include "param.h"

#define ENV_RTZ_TIME_MS (10.0f)

static void recalculate_coefficients(struct env *env);
static void env_state_off(struct env *env);
static void env_state_attack(struct env *env);
static void env_state_decay(struct env *env);
static void env_state_sustain(struct env *env);
static void env_state_release(struct env *env);
static void env_state_shutdown(struct env *env);
static float env_mode_normal(float level, float sustain);
static float env_mode_biased(float level, float sustain);
static float env_mode_inverted(float level, float sustain);
static float env_mode_biased_inverted(float level, float sustain);

static void (*env_state_handlers[ENV_MAX])(struct env *env) = {
    env_state_off,     env_state_attack,  env_state_decay,
    env_state_sustain, env_state_release, env_state_shutdown};

static float (*env_transforms[])(float level, float sustain) = {
    env_mode_normal, env_mode_biased, env_mode_inverted,
    env_mode_biased_inverted};

/*
 * Initialises the envelope
 *
 * This is Nigel Redmon's 'analogue style' algorithm.
 *
 * The time constant overshoot (tco) values ensure that the envelope
 * segments (which would be asymtoptic) will overshoot the 0
 * value indicating their end.
 */
void env_init(struct env *env) {
  RTT_ASSERT(env);

  env->attack_tco = expf(-1.5f);
  env->decay_tco = expf(-4.95f);
  env->release_tco = env->decay_tco;
  env->attack_scaler = 1.0f;
  env->decay_scaler = 1.0f;

  env_reset(env);
}

/*
 * Resets the envelope
 */
void env_reset(struct env *env) {
  RTT_ASSERT(env != NULL);

  env->state = ENV_OFF;
  env->level = 0;
}

/*
 * Renders one block of amplitude samples.
 *
 * Calls the appropriate state handler function directly using the jump
 * table indexed on envelope state and applies a transform.
 */
void env_render(struct env *env, float *output) {
  RTT_ASSERT(env != NULL);
  RTT_ASSERT(output);

  env_state_handlers[env->state](env);

  *output = env_transforms[env->mode](env->level, env->sustain);
}

/*
 * Calculate the segment curves and start the FSM.
 *
 * Attack and decay are scaled by velocity/pitch if tracking is enabled
 * which forces us to recalculate the coefficients every note_on. This
 * is computationally expensive but tracking is a "must" on most
 * synthesisers.
 */
void env_note_on(struct env *env, uint8_t midi_note, uint8_t midi_velocity) {
  RTT_ASSERT(env != NULL);

  env->attack_scaler =
      env->velocity_track ? 1.0f - midi_velocity / 127.0f : 1.0f;
  env->decay_scaler = env->note_track ? 1.0f - midi_note / 127.0f : 1.0f;
  recalculate_coefficients(env);

  env->state = ENV_ATTACK;
}

/*
 * Triggers the release phase when a key is released
 * Transitions to RELEASE state if level is above zero, otherwise
 * to OFF state.
 */
void env_note_off(struct env *env) {
  RTT_ASSERT(env != NULL);

  if (env->level > 0.0f) {
    env->state = ENV_RELEASE;
  } else {
    env->state = ENV_OFF;
  }
}

/*
 * Return To Zero (rapidly silences envelope)
 *
 * Used during voice stealing operations when a voice needs to be
 * quickly repurposed for a new note. Creates a short linear ramp
 * to zero regardless of current envelope state.
 */
void env_rtz(struct env *env) {
  RTT_ASSERT(env != NULL);

  if (env->level > 0.0f) {
    /* Number of blocks needed to fade to 0 over ENV_RTZ_TIME_MS, floored
     * to at least one block */
    const float shutdown_blocks = fmaxf(ENV_RTZ_TIME_MS * AUDIO_SAMPLE_RATE /
                                            (1000.0f * AUDIO_BLOCK_SIZE),
                                        1.0f);

    /* Compute the decrement step per block */
    env->inc_shutdown = -(env->level / shutdown_blocks);

    /* Move to shutdown state */
    env->state = ENV_SHUTDOWN;
  } else {
    /* Already silent - nothing to ramp down. Force to OFF */
    env->state = ENV_OFF;
  }
}

/*
 * Updates internal state on parameter change
 *
 * The ADR parameters would typically use a power curve however I find
 * linear mapping to be more to my taste, especially for rotary controls.
 */
void env_update(struct env *env, float attack, float decay, float sustain,
                float release, enum env_mode mode, bool note_track,
                bool velocity_track) {
  env->attack = param_uni_to_linear(attack, ENV_TIME_MIN, ENV_TIME_MAX);

  env->decay = param_uni_to_linear(decay, ENV_TIME_MIN, ENV_TIME_MAX);

  env->sustain = sustain;

  env->release = param_uni_to_linear(release, ENV_TIME_MIN, ENV_TIME_MAX);

  env->mode = mode;

  env->note_track = note_track;
  env->velocity_track = velocity_track;

  recalculate_coefficients(env);
}

/*
 * Calculates the envelope segment coefficients for segments.
 *
 * Modified (from Nigel Redmon's) for block processing.
 */
static void recalculate_coefficients(struct env *env) {
  /* Floor of 1 block guards against possible divide by zero */
  const float attack_blocks =
      fmaxf(env->attack * env->attack_scaler * AUDIO_SAMPLE_RATE /
                (1000.0f * AUDIO_BLOCK_SIZE),
            1.0f);
  const float decay_blocks =
      fmaxf(env->decay * env->decay_scaler * AUDIO_SAMPLE_RATE /
                (1000.0f * AUDIO_BLOCK_SIZE),
            1.0f);
  /*
   * Floored at one block like attack and decay.  A zero release would
   * otherwise divide by zero below.  The release state never uses the
   * coefficient when the release is zero, but release builds use -ffast-math,
   * which lets the compiler assume no division by zero or infinity ever
   * occurs, so the calculation itself must stay finite.
   */
  const float release_blocks =
      fmaxf(env->release * AUDIO_SAMPLE_RATE / (1000.0f * AUDIO_BLOCK_SIZE),
            1.0f);

  /* Compute base coefficients using block-based calculations */
  env->attack_coeff =
      expf(-logf((1.0f + env->attack_tco) / env->attack_tco) / attack_blocks);
  env->attack_overshoot = (1.0f + env->attack_tco) * (1.0f - env->attack_coeff);

  env->decay_coeff =
      expf(-logf((1.0f + env->decay_tco) / env->decay_tco) / decay_blocks);
  env->decay_overshoot =
      (env->sustain - env->decay_tco) * (1.0f - env->decay_coeff);

  env->release_coeff = expf(
      -logf((1.0f + env->release_tco) / env->release_tco) / release_blocks);
  env->release_overshoot = -env->release_tco * (1.0f - env->release_coeff);
}

/*
 * Implement OFF state - the final envelope state where output is zero
 */
static void env_state_off(struct env *env) { env->level = 0.0f; }

/*
 * Implement ATTACK state - initial envelope segment with rising amplitude
 *
 * Automatically transitions to DECAY state when:
 *       - Level reaches 1.0 (full amplitude)
 *       - Attack time is zero (immediate attack)
 */
static void env_state_attack(struct env *env) {
  if (env->attack == 0) {
    env->level = 1.0f;
    env->state = ENV_DECAY;
    return;
  }

  env->level = env->level * env->attack_coeff + env->attack_overshoot;

  if (env->level >= 1.0f) {
    env->level = 1.0f;
    env->state = ENV_DECAY;
  }
}

/*
 * Implements the DECAY state - segment where amplitude falls to sustain level
 *
 * Automatically transitions to SUSTAIN state when:
 *       - Level reaches the sustain level
 *       - Decay time is zero (immediate decay)
 *
 * If the decay is 0 then it jumps directly to the sustain without calculating
 * anything, this is to avoid a single block being calculated even if the decay
 * is 0.
 */
static void env_state_decay(struct env *env) {

  if (env->decay <= 0) {
    env->level = env->sustain;
    env->state = env->sustain > 0 ? ENV_SUSTAIN : ENV_RELEASE;
    return;
  }

  env->level = env->level * env->decay_coeff + env->decay_overshoot;
  if (env->level <= env->sustain) {
    env->level = env->sustain;
    env->state = env->sustain > 0 ? ENV_SUSTAIN : ENV_RELEASE;
  }
}

/*
 * Implements the SUSTAIN state - maintains constant level until note-off
 *
 * This state has no automatic transition - it remains active until explicitly
 * changed by a note-off event triggering the RELEASE state.
 */
static void env_state_sustain(struct env *env) { env->level = env->sustain; }

/*
 * Implements the RELEASE state - decreasing amplitude after note-off
 *
 * Triggered by note-off event, not by an automatic state transition.
 * Transitions to OFF state when amplitude reaches zero or release time is zero.
 */
static void env_state_release(struct env *env) {
  if (env->release <= 0.0f) {
    env->level = 0.0f;
    env->state = ENV_OFF;
    return;
  }

  env->level = env->level * env->release_coeff + env->release_overshoot;

  if (env->level <= 0.0f) {
    env->level = 0.0f;
    env->state = ENV_OFF;
  }
}

/*
 * Implements rapid amplitude fade to zero for voice termination
 *
 * Used during voice stealing or RTZ (return to zero) operations when
 * a voice needs to be quickly silenced. Uses linear ramp rather than
 */
static void env_state_shutdown(struct env *env) {
  env->level += env->inc_shutdown;

  /* Off */
  if (env->level <= 0.0f) {
    env->level = 0.0f; /* Clear any overshoot caused by RTZ */
    env->state = ENV_OFF;
  }
}

/* output transformer, normal envelope */
static float env_mode_normal(float level, float sustain) { return level; }

/*  output transformer, biased envelope (for pitch modulation)*/
static float env_mode_biased(float level, float sustain) {
  return level - sustain;
}

/* output transformer, inverted envelope */
static float env_mode_inverted(float level, float sustain) {
  return 1.0f - level;
}

/*  output transformer, biased and inverted envelope */
static float env_mode_biased_inverted(float level, float sustain) {
  return (1.0f - level) - sustain;
}
