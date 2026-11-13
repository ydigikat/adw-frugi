# Frugi User Manual

Frugi is an 8-voice subtractive synthesiser for the STM32F411 "Black Pill" board, played and programmed over MIDI. It is the example project for the article series *"Audio Doesn't Wait"*.

This manual is in two parts:

- **[Part 1: The synthesiser](#part-1--the-synthesiser)** covers everything you can do over MIDI. It applies to every Frugi, and it is the whole story for the **headless** build, which has no display.
- **[Part 2: The front panel](#part-2--the-front-panel)** covers the optional display, controls and patch storage in the **full** build.

For building and flashing the firmware, see the [README](../README.md).

---

## Contents

**Part 1: The synthesiser**
1. [Introduction](#1-introduction)
2. [Getting started](#2-getting-started)
3. [How Frugi makes a sound](#3-how-frugi-makes-a-sound)
4. [Oscillators](#4-oscillators)
5. [Mixer: noise, ring modulator and shaper](#5-mixer-noise-ring-modulator-and-shaper)
6. [Filter](#6-filter)
7. [Amplifier and amp envelope](#7-amplifier-and-amp-envelope)
8. [Modulation: mod envelope, LFO and routing](#8-modulation-mod-envelope-lfo-and-routing)
9. [Performance: voice modes, portamento and controllers](#9-performance-voice-modes-portamento-and-controllers)
10. [Patches over MIDI](#10-patches-over-midi)
11. [MIDI reference](#11-midi-reference)
12. [Parameter reference](#12-parameter-reference)
13. [Troubleshooting](#13-troubleshooting)

**Part 2: The front panel**

14. [Controls](#14-controls)
15. [The ring menu: editing sounds](#15-the-ring-menu-editing-sounds)
16. [The patch menu: storing sounds](#16-the-patch-menu-storing-sounds)

**Appendix**
- [A. The default patch](#a-the-default-patch)

---

# Part 1: The synthesiser

## 1. Introduction

Frugi is a "virtual analogue" polysynth in the style of the classic early-1980s instruments. Each voice has:

- 2 oscillators
- a noise source
- a ring modulator
- a waveshaper
- a resonant multi-mode filter
- an amplifier with its own envelope
- a modulation envelope and an LFO, routed through 6 fixed modulation paths

The voices are mixed and then pass through a stereo chorus.

| | |
|---|---|
| Polyphony | 8 voices (6 in debug builds) |
| Voice modes | Poly, or Mono with unison (all voices stacked on one note) |
| Oscillators | 2 per voice: triangle, saw, pulse with PWM |
| Filter | Ladder style; LP, BP or HP, each in 2-pole or 4-pole form; resonance, drive, key tracking |
| Envelopes | 2 ADSR per voice (amp and mod) |
| LFO | 1 per voice, 5 simultaneous waves, 0–20 Hz |
| Effects | Stereo chorus |
| Audio | 48 kHz stereo over I2S |
| Control | MIDI In (omni). Every parameter has a CC. |

### Headless and full builds

| | Headless | Full |
|---|---|---|
| Sound engine and MIDI | ✓ | ✓ |
| Display, encoder and buttons | | ✓ (Part 2) |
| Patch memory (128 slots) | | ✓ (Part 2) |
| Program Change | Loads the default patch | Loads a stored patch |

Everything in Part 1 works the same in both builds.

## 2. Getting started

### Connections

| Connection | Notes |
|---|---|
| **MIDI In** | 5-pin DIN, opto-isolated |
| **Audio Out** | Stereo line level, from the UDA1334A DAC |
| **Power** | USB-C on the Black Pill board |

### Indicators

| LED | Meaning |
|---|---|
| **Heartbeat** (strobe) | A short flash once a second: the synth is running normally |
| **Fault** | Lit: Frugi stopped because of an internal error, or the watchdog reset it (release builds). If it stays lit, power-cycle the synth. |

### First sound

1. Connect a MIDI keyboard or a DAW to MIDI In, and connect Audio Out to an amplifier or mixer.
2. Power up. Frugi always starts with the **default patch**: a bright, unfiltered saw-plus-triangle tone with an organ-like envelope. Appendix A lists its full settings.
3. Play. Frugi responds on **every MIDI channel**.

Some quick things to try:
- **CC74** (filter cutoff): turn it down to darken the sound.
- **CC71** (resonance): turn it up for a resonant peak.
- **CC72** (amp release): turn it up so notes fade out after you let go.
- **CC116** (voice mode): send 127 for mono/unison, 0 for poly.
- **CC93** (chorus): send 127 for the stereo chorus.

## 3. How Frugi makes a sound

Each voice follows the classic subtractive signal path:

```
                 MODULATION  (mod env, LFO, mod wheel)
                   │ pitch, PWM        │ cutoff          │ level
                   ▼                   ▼                 ▼
 ┌──────┐     ┌─────────┐  ┌───────┐  ┌────────┐       ┌─────┐
 │ DCO1 │──┬─►│ MIXER + │  │       │  │        │       │     │
 └──────┘  │  │  RING   │─►│SHAPER │─►│ FILTER │──────►│ AMP │──► voice
 ┌──────┐  └─►│   MOD   │  │       │  │  (DCF) │       │(DCA)│
 │ DCO2 │────►│         │  └───────┘  └────────┘       └─────┘
 └──────┘     └─────────┘      ▲                          ▲
                   NOISE ──────┘                      AMP ENVELOPE
```

The voices are then mixed together:

```
voices 1–8 ──► mix ──► chorus (optional) ──► stereo out
```

Each voice is mono. When the chorus is on, it creates the stereo image.

The sections that follow work along this path from left to right. Section 12 lists every parameter with its CC and range.

> **How values are described in this manual.** "0–100 %" means a continuous control: 0 % is CC value 0 and 100 % is CC value 127. On/off switches turn on at CC 64 and above. Selectors (such as waveform) are spread evenly across 0–127; the values to send are listed in [section 11.4](#114-sending-selector-and-stepped-values).

## 4. Oscillators

Each voice has two digitally controlled oscillators, **DCO1** and **DCO2**, with identical controls.

| Parameter | Range | What it does |
|---|---|---|
| Wave | TRI, SAW, PULSE | Triangle is soft and hollow. Saw is bright and buzzy. Pulse is reedy, and its tone depends on the pulse width. |
| Level | 0–100 % | The oscillator's level in the mixer |
| Octave | −2 … +2 | Coarse tuning in octaves |
| Semitone | −6 … +6 | Tuning in semitones |
| Fine tune | ±50 cents | Detune. The centre (CC 64) is in tune, to within half a cent. |
| Pulse width | 5–95 % | Shape of the pulse wave. 50 % is a square wave; narrower settings sound thinner and more nasal. |
| Pitch mod source / depth | LFO wave or ENV / 0–100 % | Vibrato or pitch sweeps. Full depth is ±1 octave. |
| PWM source / depth | LFO wave or ENV / 0–100 % | Moves the pulse width, for the classic "chorused" pulse sound |

Tips:
- Detune DCO2 by a few cents against DCO1 for a fat, slowly beating sound.
- Set DCO2 to +1 octave, or to a fifth (+7 semitones, entered as +1 octave and −5 semitones), for organ-like intervals.
- PWM only affects the pulse wave.

## 5. Mixer: noise, ring modulator and shaper

| Parameter | Range | What it does |
|---|---|---|
| DCO1 level, DCO2 level | 0–100 % | Oscillator levels (see section 4) |
| Noise level | 0–100 % | White noise, for breath, wind, percussion and so on |
| Ring-Mod | 0–100 % | Fades DCO2's mixer channel from DCO2 itself (0 %) to the ring modulator (100 %) |
| Shaper drive | 0–100 % | Waveshaping distortion of the mix, from slight rounding to heavy saturation |

### Using the ring modulator

A ring modulator multiplies DCO1 by DCO2. The two original pitches disappear, and you hear their sum and difference frequencies instead. The result is metallic and bell-like: the classic clangorous ring-mod sound.

- **Pure ring mod:** set Ring-Mod to 100 % and **DCO1 level to 0**. DCO1 still feeds the ring modulator, but it is no longer heard on its own.
- **Ring mod over a fundamental:** keep DCO1's level up, so the note's pitch stays clear underneath the ring sound.
- **The ring level** is set by DCO2's level.
- **Tune DCO2 to an inharmonic interval.** At unison or an octave, multiplying the oscillators only adds harmonics that are already there, so you hear very little change. At a fifth it just sounds like a lower octave. Try a few semitones plus a few cents, for example DCO2 at +3 semitones and +13 cents. Sweeping DCO2's pitch with the LFO or the mod envelope gives the classic ring-mod sweep.

## 6. Filter

The filter (DCF) is ladder style and resonant, and has six modes:

| Mode | Effect |
|---|---|
| LP2 / LP4 | Low-pass: removes the highs. LP4 is steeper and more "classic". |
| BP2 / BP4 | Band-pass: keeps a band around the cutoff, for thin, vocal sounds |
| HP2 / HP4 | High-pass: removes the lows |

| Parameter | Range | What it does |
|---|---|---|
| Cutoff | 80 Hz – 18 kHz | The filter frequency |
| Resonance | 0–100 % | Emphasis at the cutoff. High settings whistle. |
| Saturation | 0–100 % | Drives the filter. It thickens the sound and tames very resonant peaks. |
| Key tracking | on/off | The cutoff follows the keyboard, measured from middle C, so high notes stay bright |
| Mod source / depth | LFO wave or ENV / 0–100 % | Filter sweeps and wah effects. Full depth is ±2 octaves. |

> The cutoff responds to modulation from the mod envelope or LFO (section 8). For a classic plucked bass, set a low cutoff, select ENV as the filter's mod source with a moderate depth, and give the mod envelope a short decay.

## 7. Amplifier and amp envelope

The amplifier (DCA) sets the voice's final level and shapes each note with the **amp envelope**, which is a standard ADSR.

| Parameter | Range | What it does |
|---|---|---|
| Level | 0–100 % | Overall volume (CC7) |
| Attack | 0–10 s | Time to reach full level after a key is pressed |
| Decay | 0–10 s | Time to fall to the sustain level |
| Sustain | 0–100 % | The level held while the key is down |
| Release | 0–10 s | Time to fade out after the key is released |
| Mod source / depth | LFO wave or ENV / 0–100 % | Tremolo, or extra level shaping |
| Note tracking | on/off | Higher notes decay faster, like a plucked string |
| Velocity tracking | on/off | Harder playing gives a faster attack |

> **Velocity and loudness.** Frugi uses velocity to shape the envelope (with Velocity tracking on), not to set the level. Every note plays at the same volume, as on many vintage synths.

## 8. Modulation: mod envelope, LFO and routing

### Mod envelope

The mod envelope is a second ADSR. It drives modulation rather than the volume.

| Parameter | Range | What it does |
|---|---|---|
| Attack, Decay, Release | 0–10 s | As for the amp envelope |
| Sustain | 0–100 % | As for the amp envelope |
| Mode | NORMAL, BIAS, INV, INV+BIAS | How the envelope is applied (see below) |

| Mode | Output |
|---|---|
| NORMAL | Rises from 0 to peak, falls to sustain, and returns to 0 on release |
| INV | Upside down: it dips instead of rising |
| BIAS | Measured from the sustain level, so the held part of the note adds nothing. Use it for pitch, so the note sweeps and then settles in tune. |
| INV+BIAS | Inverted and biased |

### LFO

Each voice has one LFO. It produces all five waves at once at a single rate, and each modulation route chooses which wave it uses.

| Parameter | Range | What it does |
|---|---|---|
| Rate | 0–20 Hz | LFO speed |
| Mode | NOTE, FREE | NOTE restarts the LFO on each new note. FREE lets it run continuously. |

| Wave | Shape |
|---|---|
| TRI | Smooth up and down: vibrato, gentle sweeps |
| SAW | Rising ramp |
| RAMP | Falling ramp |
| SQUARE | Switches between two values: trills |
| S&H | A random step on each cycle (sample-and-hold) |

### Modulation routing

Frugi has six fixed modulation routes. Each one has its own **source** (one of the five LFO waves, or ENV, the mod envelope) and **depth**:

| Destination | Source / depth parameters | Full-depth range |
|---|---|---|
| DCO1 pitch | DCO1 pitch mod source / depth | ±1 octave |
| DCO1 pulse width | DCO1 PWM source / depth | the full pulse-width range |
| DCO2 pitch | DCO2 pitch mod source / depth | ±1 octave |
| DCO2 pulse width | DCO2 PWM source / depth | the full pulse-width range |
| Filter cutoff | DCF mod source / depth | ±2 octaves |
| Amp level | DCA mod source / depth | tremolo |

A depth of 0 turns a route off.

## 9. Performance: voice modes, portamento and controllers

### Voice modes

| Mode | Behaviour |
|---|---|
| **POLY** | Each note gets its own voice. If you play more notes than there are voices, Frugi steals one: first a voice that is already fading out, then the oldest note held only by a pedal, then the oldest held note. |
| **MONO** | All voices play the same note in **unison**: one huge voice. Playing a new note while still holding the last one slides legato, without restarting the envelopes. Releasing it returns to the note you are still holding, which makes trills easy. |

**Unison detune** (0–100 cents) spreads the stacked voices apart in pitch when in MONO mode. Small values thicken the sound; large values give a dramatic "supersaw" spread. Most of the control's travel is in the subtle range.

### Portamento

With **Portamento** on, the pitch glides from one note to the next. **Time** (up to 5 s) sets how long the glide takes.

### Pitch bend and mod wheel

| Control | Behaviour |
|---|---|
| **Pitch bend** | Bends all voices. The range is set by **Bend range** (CC106, 0–24 semitones). The default is ±2. |
| **Mod wheel** (CC1) | Increases modulation that is **already set up**: at full wheel, the selected routes have up to twice their depth. **Mod wheel destination** chooses VCO (oscillator pitch routes), VCF (filter cutoff route) or VCO+VCF. |

> The mod wheel has no effect if the route's depth is 0. For wheel-controlled vibrato, set a small DCO pitch depth from the LFO TRI wave.

### Pedals

| Pedal | Behaviour |
|---|---|
| **Sustain** (CC64) | Holds every note while the pedal is down |
| **Sostenuto** (CC66) | Holds only the notes that were down when you pressed the pedal |

### Chorus

**Chorus** (on/off) adds a slow, wide stereo chorus after the voices are mixed, in the style of a Juno.

## 10. Patches over MIDI

A Frugi sound (a *patch*) is simply the current setting of every parameter in section 12. Over MIDI you can:

- **Create a sound** by sending CCs. Every parameter has one, and a DAW automation lane or a hardware controller works well.
- **Reset to the default patch** with Program Change. In a headless build, every Program Change loads the default patch. In a full build, Program Change *n* loads stored patch slot *n*, and an empty slot loads the default patch (see Part 2).
- **Store a sound** as a DAW preset or MIDI file: send the full set of CCs at the start of a track.

### Soft takeover

When a hardware knob's position doesn't match the synth's current value, moving the knob would normally make the parameter jump. With **soft takeover** on (CC109 at 64 or above), Frugi ignores each control until it reaches or passes the parameter's current value, and then follows it smoothly. Send CC109 below 64 to turn it off. It is off when Frugi starts.

## 11. MIDI reference

### 11.1 Implementation chart

| Function | Recognised | Remarks |
|---|---|---|
| Basic channel | All (omni) | Fixed |
| Mode | Omni on, poly or mono | Set by CC116 or CC126/127 |
| Note number | 0–127 | |
| Velocity: note on | ✓ | Shapes the amp attack when Velocity tracking is on |
| Velocity: note off | ✗ | |
| Aftertouch | ✗ | |
| Pitch bend | ✓ | 14-bit; range set by CC106 |
| Control change | ✓ | See 11.2 and 11.3 |
| Program change | ✓ 0–127 | Headless: loads the default patch. Full: loads that patch slot. |
| System exclusive | ✗ | |
| System common / real time | ✗ | |
| System reset (0xFF) | ✓ | Silence, forget held keys and pedals, centre bend and wheel. The patch is kept. |

### 11.2 Performance and channel-mode controllers

| CC | Function |
|---|---|
| 1 | Mod wheel |
| 64 | Sustain pedal |
| 66 | Sostenuto pedal |
| 109 | Soft takeover on/off |
| 120 | All sound off: silence immediately and forget held keys and pedals (panic) |
| 121 | Reset all controllers: release both pedals, centre pitch bend and mod wheel |
| 123 | All notes off: release every key as if lifted (pedals still hold) |
| 124 / 125 | Omni off / on: not supported; treated as all notes off |
| 126 / 127 | Mono / poly on: all notes off, then set the voice mode |

### 11.3 Parameter controllers

| CC | Parameter | CC | Parameter |
| --- | --- | --- | --- |
| 5 | Portamento time | 59 | Filter saturation (drive) |
| 7 | Amp level | 60 | Filter key tracking |
| 14 | Amp sustain | 61 | Shaper drive |
| 15 | DCO1 wave | 62 | Ring-mod level |
| 16 | DCO1 level | 65 | Portamento on/off |
| 17 | DCO1 octave | 71 | Filter resonance |
| 18 | DCO1 semitone | 72 | Amp release |
| 19 | DCO1 fine tune | 73 | Amp attack |
| 20 | DCO1 pulse width | 74 | Filter cutoff |
| 21 | DCO1 pitch mod source | 80 | Amp decay |
| 22 | DCO1 pitch mod depth | 93 | Chorus on/off |
| 23 | DCO2 wave | 94 | Unison detune |
| 24 | DCO2 level | 102 | DCO1 PWM source |
| 25 | DCO2 octave | 103 | DCO1 PWM depth |
| 26 | DCO2 semitone | 104 | DCO2 PWM source |
| 27 | DCO2 fine tune | 105 | DCO2 PWM depth |
| 28 | DCO2 pulse width | 106 | Pitch bend range |
| 29 | DCO2 pitch mod source | 107 | Noise level |
| 30 | DCO2 pitch mod depth | 108 | Mod wheel destination |
| 31 | Mod env mode | 110 | Amp mod source |
| 52 | Mod env attack | 111 | Amp mod depth |
| 53 | Mod env decay | 112 | Amp note tracking |
| 54 | Mod env sustain | 113 | Amp velocity tracking |
| 55 | Mod env release | 114 | Filter mod source |
| 56 | LFO rate | 115 | Filter mod depth |
| 57 | LFO mode | 116 | Voice mode |
| 58 | Filter mode | | |

### 11.4 Sending selector and stepped values

Selectors and stepped parameters divide the 0–127 range evenly. These values select each setting exactly:

| Parameter | CC values → setting |
|---|---|
| On/off switches | 0 = off, 127 = on (on at 64 and above) |
| DCO wave | 0 TRI · 64 SAW · 127 PULSE |
| Mod source (all routes) | 0 TRI · 25 SAW · 51 RAMP · 76 SQUARE · 102 S&H · 127 ENV |
| Filter mode | 0 LP2 · 25 BP2 · 51 HP2 · 76 LP4 · 102 BP4 · 127 HP4 |
| Mod env mode | 0 NORMAL · 42 BIAS · 85 INV · 127 INV+BIAS |
| LFO mode | 0 FREE · 127 NOTE |
| Voice mode | 0 POLY · 127 MONO |
| Mod wheel destination | 0 VCO · 64 VCO+VCF · 127 VCF |
| DCO octave | 0 = −2 · 32 = −1 · 64 = 0 · 95 = +1 · 127 = +2 |
| DCO semitone | 0 = −6 … 64 = 0 … 127 = +6, about 10.6 per semitone |
| DCO fine tune | 0 = −50 cents · 64 = in tune (+0.4 cents) · 127 = +50 cents |
| Bend range | 0–24 semitones, about 5.3 per semitone (11 gives ±2) |

## 12. Parameter reference

| Section | Parameter | CC | Range / values | Default |
|---|---|---|---|---|
| DCO1 | Wave | 15 | TRI, SAW, PULSE | SAW |
| | Level | 16 | 0–100 % | 100 % |
| | Octave | 17 | −2 … +2 | 0 |
| | Semitone | 18 | −6 … +6 | 0 |
| | Fine tune | 19 | ±50 cents | 0 |
| | Pulse width | 20 | 5–95 % | 50 % |
| | Pitch mod source / depth | 21 / 22 | mod source / 0–100 % | SAW / 0 |
| | PWM source / depth | 102 / 103 | mod source / 0–100 % | TRI / 0 |
| DCO2 | Wave | 23 | TRI, SAW, PULSE | TRI |
| | Level | 24 | 0–100 % | 100 % |
| | Octave | 25 | −2 … +2 | 0 |
| | Semitone | 26 | −6 … +6 | 0 |
| | Fine tune | 27 | ±50 cents | 0 |
| | Pulse width | 28 | 5–95 % | 50 % |
| | Pitch mod source / depth | 29 / 30 | mod source / 0–100 % | RAMP / 0 |
| | PWM source / depth | 104 / 105 | mod source / 0–100 % | TRI / 0 |
| Mixer | Noise level | 107 | 0–100 % | 0 |
| | Shaper drive | 61 | 0–100 % | 0 |
| | Ring-Mod | 62 | 0–100 % | 0 |
| Filter | Mode | 58 | LP2, BP2, HP2, LP4, BP4, HP4 | LP2 |
| | Cutoff | 74 | 80 Hz – 18 kHz | fully open |
| | Resonance | 71 | 0–100 % | 0 |
| | Saturation | 59 | 0–100 % | 0 |
| | Key tracking | 60 | on/off | off |
| | Mod source / depth | 114 / 115 | mod source / 0–100 % | TRI / 0 |
| Mod env | Attack / Decay / Release | 52 / 53 / 55 | 0–10 s | 0 / 0 / 5 s |
| | Sustain | 54 | 0–100 % | 100 % |
| | Mode | 31 | NORMAL, BIAS, INV, INV+BIAS | NORMAL |
| LFO | Rate | 56 | 0–20 Hz | 10 Hz |
| | Mode | 57 | FREE, NOTE | NOTE |
| Amp | Level | 7 | 0–100 % | 100 % |
| | Attack / Decay / Release | 73 / 80 / 72 | 0–10 s | 0 / 0 / 0 |
| | Sustain | 14 | 0–100 % | 100 % |
| | Mod source / depth | 110 / 111 | mod source / 0–100 % | SAW / 0 |
| | Note tracking | 112 | on/off | off |
| | Velocity tracking | 113 | on/off | off |
| Performance | Voice mode | 116 | POLY, MONO | POLY |
| | Unison detune | 94 | 0–100 cents | 0 |
| | Portamento on/off | 65 | on/off | off |
| | Portamento time | 5 | 0–5 s | 0 |
| | Bend range | 106 | 0–24 semitones | 2 |
| | Mod wheel destination | 108 | VCO, VCO+VCF, VCF | VCO |
| | Chorus | 93 | on/off | off |

"Mod source" means one of TRI, SAW, RAMP, SQUARE, S&H (the LFO waves) or ENV (the mod envelope).

## 13. Troubleshooting

| Symptom | Check |
|---|---|
| No sound | Amp level (CC7) and the DCO levels are not 0, the filter cutoff (CC74) is not fully closed, the heartbeat LED is flashing, and the MIDI cable goes into MIDI **In**. |
| Notes stuck on | Send CC120 (all sound off) or CC123 (all notes off). Check that the sustain pedal is not held (CC64). |
| A knob does nothing at first, then jumps in | Soft takeover is on (CC109). Move the knob through the current value. |
| The mod wheel does nothing | It only scales routes whose depth is above 0 (section 9) |
| Ring mod sounds weak | Tune DCO2 to an inharmonic interval, and turn DCO1's level down for pure ring (section 5) |
| Distortion at high resonance | Add filter saturation, or lower the amp level |
| The fault LED is lit | Power-cycle. If it happens again, see the README's notes on RTT logging. |

---

# Part 2: The front panel

*This part applies only to the full build.*

## 14. Controls

```
        ┌──────────────────────────────┐
        │  [DCF  ]<NOISE M-ENV>        │ ← status bar
        │  Cutoff               100%   │ ← selected field (inverted)
        │  Resonance              0%   │
        │  …                           │
        │  (hint row)                  │
        └──────────────────────────────┘
                     N
                 W   C   E        ◯ encoder (C = encoder push)
                     S
```

| Control | Ring menu (editing) | Patch menu |
|---|---|---|
| **E / W** | Next / previous page | Jump ±10 patch slots, or move the cursor when naming |
| **N / S** | Previous / next field | Switch between the Patch # and Action fields |
| **Encoder** | Change the selected value | Change the selected field, or the character when naming |
| **C double-click** | Open the patch menu | Close the patch menu |
| **C hold** (about 0.6 s) | (none) | Confirm: load, save or delete |

A double-click is two presses within about 0.4 s.

## 15. The ring menu: editing sounds

The ring menu is always ready while you play. Changes take effect immediately, and nothing has to be confirmed. The pages form a loop: pressing E after the last page brings you back to the first. The status bar shows the current page in brackets, with the previous and next pages beside it. Frugi starts on the **DCF** page.

| Page | Fields |
|---|---|
| **DCO1** | Wave, Level, Octave, Semi, Cents, Pulse Width |
| **MOD1** | Mod Source, Mod Depth, PWM Source, PWM Depth (all for DCO1) |
| **DCO2** | Wave, Level, Octave, Semi, Cents, Pulse Width |
| **MOD2** | Mod Source, Mod Depth, PWM Source, PWM Depth (all for DCO2) |
| **NOISE** | OSC Drive (shaper), Noise Level, Ring-Mod |
| **DCF** | Cutoff, Resonance, Saturation, Mode, Track, Mod Source, Mod Depth |
| **M-ENV** | Attack, Decay, Sustain, Release, Mode |
| **LFO** | LFO1 Rate, LFO1 Mode |
| **DCA** | Level, Attack, Decay, Sustain, Release, MOD Source, MOD Depth, Note Track, Vel Track |
| **PERF** | Chorus, Mod Wheel, Voice Mode, Portamento, Time, Uni-Detune, Bend |

### How values are shown and stepped

| Type | Shown as | One encoder step |
|---|---|---|
| Continuous | 0–100 % | 1 % |
| Stepped (Octave, Semi) | +n / −n | 1 |
| On/off | ON / OFF | Toggles, in either direction |
| Selector | Name (SAW, LP4 …) | Next or previous; wraps around |

Some continuous fields show a percentage of their range rather than real units:
- **Cents:** 50 % is in tune, and each step is 1 cent.
- **Bend:** a percentage of 24 semitones. 8 % is about ±2 semitones.
- **Time** (portamento) and the envelope times: a percentage of 5 s and 10 s respectively.

### Hints

Some fields show a hint on the bottom row when it would help. With **Ring-Mod** in use:
- **"Detune DCO2 to ring"** appears when DCO2 is at unison, an octave or a fifth from DCO1, where ring modulation sounds weak.
- **"DCO1 Lvl 0: pure RM"** reminds you that muting DCO1 gives pure ring modulation.

## 16. The patch menu: storing sounds

Frugi stores **128 patches** (slots 000–127). Each slot holds a full sound and a name of up to 15 characters. Double-click **C** to open the patch menu:

```
  LOAD/SAVE PATCH

  Patch #          012    ← slot (select with N/S)
  BRASS PAD               ← its name, or "-- empty --"
  Action          LOAD    ← LOAD / SAVE / DELETE

  Hold C LOAD             ← what holding C will do
  Loaded.                 ← result of the last action
```

### Choosing a slot and action

- Select **Patch #** with N/S, then turn the encoder to step through the slots (it wraps from 127 to 000). Press E or W to jump 10 slots at a time.
- Select **Action** with N/S, then turn the encoder to choose LOAD, SAVE or DELETE.
- **Hold C** to carry out the action.

### Loading

**LOAD** replaces the current sound with the stored one. Any changes you hadn't saved are lost. If a slot is empty or unreadable, "Load failed." is shown and the current sound is kept.

### Saving

**SAVE** stores the current sound, including any edits you have made, in the selected slot.

1. Choose SAVE and **hold C**. The name editor opens, showing the slot's existing name if it has one.
2. Turn the encoder to change the character under the cursor (space, A–Z, 0–9, −). Press E or W to move the cursor.
3. **Hold C** to save. "Saved." confirms it.

Saving overwrites whatever was in the slot. To abandon the save, double-click C to leave the menu. The audio goes silent for about 20 ms while the patch is written.

### Deleting

Choose DELETE and **hold C**. The screen asks "Delete slot nnn?". **Hold C** again to delete, or press any other control to cancel.

### Patches and Program Change

A MIDI Program Change *n* loads slot *n*, so slots 000–127 match program numbers 0–127 (some devices number them 1–128). An empty slot loads the default patch.

> **Firmware updates.** Patches are stored with a format version. If a firmware update changes the set of parameters, patches saved by older firmware can no longer be loaded. Before updating, make a note of any sounds you want to keep, or capture them as CC sequences.

---

# Appendix

## A. The default patch

Frugi starts with this patch, and a Program Change loads it in a headless build or for an empty slot. It is a plain, bright starting point:

| Section | Settings |
|---|---|
| DCO1 | SAW, level 100 %, in tune; pitch and PWM depth 0 |
| DCO2 | TRI, level 100 %, in tune; pitch and PWM depth 0 |
| Mixer | Noise 0, shaper 0, ring-mod 0 |
| Filter | LP2, cutoff fully open, resonance 0, saturation 0, tracking off, mod depth 0 |
| Amp | Level 100 %; A 0, D 0, S 100 %, R 0 (an organ-style gate); mod depth 0; tracking off |
| Mod env | A 0, D 0, S 100 %, R 5 s, NORMAL |
| LFO | 10 Hz, NOTE mode |
| Performance | POLY; portamento off; unison detune 0; bend ±2 semitones; mod wheel → VCO; chorus off |
