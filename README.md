
# Frugi

> A.I Disclosure:  Anthropic Claude was used for writing some of this README.md, all of the user manual and the unit tests, it also acted as code reviewer on occasion.  All functional code, control-plane, DSP and hardware support was written by me.  

I name my projects using Roman names. 

Frugi (meaning frugal, or perhaps better, honest) is a complete subtractive digital synthesiser for the STM32F411CEU6 "Black Pill" board. It has 8 voices, 2 oscillators per voice, a resonant multi-mode filter, envelopes, an LFO and a stereo chorus, and it is played and programmed over MIDI.

Frugi is the example project for the article series **"Audio Doesn't Wait"**, an introduction to synth control-plane architecture. *(Link to follow on publication.)*

- **Playing Frugi:** features, parameters, the MIDI implementation and the front panel are covered in the **[User Manual](docs/user-manual.md)**.
- **How it works:** see the article series.
- **This README** covers building the hardware and firmware.

## About me

I am an embedded engineer, approaching retirement after 40+ years commercial experience.  This is the first in a series of projects and articles/books which I hope to use to keep my mind busy and active once my commercial challenges end.

I am also a trained musician with many years working in 1980s studios playing synthesiers and other keyboard instruments.  The vintage synths so loved today were just my tools of the trade then!

---

## Build variants

There are two variants of the firmware, chosen at compile time:

| | Headless | Full |
|---|---|---|
| Synth engine, audio, MIDI | ✓ | ✓ |
| LCD, encoder and button UI | | ✓ |
| Patch storage (EEPROM) | | ✓ |
| Presets | `debug-headless`, `release-headless` | `debug`, `release` |

Frugi was originally designed to be headless: played and programmed entirely over MIDI. The **headless** build is all you need to follow the articles, and it needs the least hardware.

---

## Hardware

| Part | Purpose | Variant |
|---|---|---|
| STM32F411CEU6 "Black Pill" (25 MHz HSE) | MCU | both |
| UDA1334A I2S DAC breakout | Stereo audio out | both |
| Opto-isolated MIDI input | MIDI In (UART1) | both |
| 2 LEDs and resistors | Heartbeat and fault indicators | both |
| ILI9341 320×240 SPI LCD | Display | full |
| 25LC256 SPI EEPROM (32 KB) | Patch storage, 128 slots | full |
| Rotary encoder with push switch, 4 push buttons | Navigation | full |

| Function | Pins |
|---|---|
| I2S2 to the DAC: SCK, WS, SD | PB10, PB12, PB15 |
| MIDI In (UART1 RX) | PA10 |
| LEDs: strobe (heartbeat), fault | PB8, PB9 |
| SPI1, shared by the LCD and EEPROM: SCK, MISO, MOSI | PA5, PA6, PA7 *(full)* |
| LCD D/C, RST, CS | PA1, PA2, PA4 *(full)* |
| EEPROM CS | PA0 *(full)* |
| Encoder A/B (TIM3) | PB4, PB5 *(full)* |
| Buttons N, E, S, W, C | PB6, PB7, PB0, PB1, PB3 *(full)* |

The full pin table, including the prototype's wire colours, is in [source/hardware/bsp/pinout.md](source/hardware/bsp/pinout.md). In a headless build the full-only pins are left unconfigured.

> **TODO:** MIDI input schematic (optocoupler and resistor values), and a photo of the prototype.

---

## Toolchain

> I develop on Linux with VS Code. These notes assume you are past the "blinky" stage of STM32 development.

| Requirement | Notes |
|---|---|
| [STM32CubeCLT](https://www.st.com/en/development-tools/stm32cubeclt.html) | ST's command-line tools: the `clang` cross-toolchain, and the ST-Link flashing tools. Set the `STM32CUBECLT_PATH` environment variable to where it is installed. |
| CMake ≥ 3.20 and Ninja | Build system |
| Host `clang` | For the unit tests only |
| A debug probe | ST-Link, or a Segger J-Link (needed for RTT tracing) |

The toolchain settings are in `cmake/stm_arm_clang.cmake` (cross-compiler) and `cmake/host_clang.cmake` (unit tests). Frugi uses the STM32 LL (low-level) drivers, which are included in `source/drivers`.

---

## Building

### Presets

| Preset | Target | Description |
|---|---|---|
| `release-headless` | MCU | Headless, full optimisation, debug information stripped |
| `debug-headless` | MCU | Headless, limited optimisation, full debug information |
| `release` | MCU | Full build (UI and patch storage), optimised |
| `debug` | MCU | Full build, debug |
| `test` | Host | Unity unit tests |

Each preset builds in its own directory, `build/<preset>/`.

### Build and flash

```sh
cmake --preset release-headless                     # configure
cmake --build --preset release-headless             # build
cmake --build --preset release-headless --target clean
```

The firmware image is `build/<preset>/build/source/app.elf`. The build prints its size when it finishes.

To flash it, use STM32CubeProgrammer's CLI, which is part of STM32CubeCLT:

```sh
PROG=$STM32CUBECLT_PATH/STM32CubeProgrammer/bin/STM32_Programmer_CLI
ELF=build/release-headless/build/source/app.elf

$PROG -c port=SWD   reset=SWrst -w $ELF -g     # ST-Link
$PROG -c port=JLink reset=SWrst -w $ELF -g     # J-Link
```

In VS Code, the **Flash** task does this for the active preset (configured for J-Link).

### Unit tests

The synth engine is generic C with no hardware dependencies, so it is unit tested on the host:

```sh
cmake --preset test
cmake --build build/test
ctest --test-dir build/test --output-on-failure
```

### VS Code

The project includes VS Code tasks for configuring, building, flashing and running the tests, and launch configurations for debugging. All of them follow the active CMake preset. `.vscode/extensions.json` lists the recommended extensions. With those installed, the VS Code tasks are the simplest way to work with the project.

---

## Build options

`source/CMakeLists.txt` controls which features are built. The defaults suit a typical build.

| Option | Default | Effect |
|---|---|---|
| `HEADLESS` | OFF (ON in the `*-headless` presets) | Leaves out the UI, the patch storage and their drivers. Program Change then loads the default patch. Can also be set directly, e.g. `cmake --preset debug -DHEADLESS=ON`. |
| `RTT_ENABLED` | ON | Segger RTT tracing and asserts. These only work with a J-Link, so for any other probe use `cmake --preset debug -DRTT_ENABLED=OFF`. |

The following compile definitions are set in `source/CMakeLists.txt` (`DEFS_APP`):

| Define | Default | Effect |
|---|---|---|
| `SYNTH_MAX_VOICES` | 6 (debug), 8 (release) | Number of voices. Only 6 and 8 are supported. |
| `WATCHDOG_ENABLED` | release only | Independent watchdog (~500 ms) |
| `STROBE_ENABLED` | on | Heartbeat LED |
| `FILTER_DRIVE` | on | Filter saturation stage |
| `TRACE_*` | off | Per-module RTT traces (`TRACE_AUDIO`, `TRACE_MIDI`, `TRACE_KEYS`, `TRACE_ASSIGN`, `TRACE_VOICE` …) |

Traces can be expensive, especially when they log floats inside a tight loop, so only enable the ones you need. In a host build, defining `TRACE_TO_STDOUT` sends the same traces to `printf`, so the voice-management layers can be followed on a PC.

---

## Repository layout

| Folder | Contents |
|---|---|
| `docs` | User manual |
| `source/app/synth/control` | Synth control plane: synth, keys, voice assigners, voices, parameters, MIDI map, default patch |
| `source/app/synth/dsp` | DSP modules: DCO, DCF, DCA, envelope, LFO, noise, shaper, chorus |
| `source/app/audio` | Audio engine |
| `source/app/midi` | MIDI parser and receive routine |
| `source/app/ui` | Display, menus and button gestures *(full build)* |
| `source/app/patch` | Patch format and EEPROM persistence *(full build)* |
| `source/hardware/bsp` | Board bring-up: clocks, FPU, watchdog, LEDs, pinout |
| `source/hardware/io` | Peripheral drivers: I2S, MIDI UART, SPI, LCD, EEPROM, encoder, buttons |
| `source/drivers` | STM32 LL drivers, CMSIS, Segger RTT |
| `source/test` | Unity unit tests (run on the host) |
| `cmake` | Toolchain files |

---

## Licences

Frugi's source code is released under the MIT licence (see `LICENSE`).

The display font table, `source/app/ui/font/font_ui.c`, is rasterised from one of two fonts. Each font has its own folder under `source/app/ui/font/`, containing its source TTF and licence. Both are distributed under the SIL Open Font License 1.1:

| Folder | Font | Copyright |
| --- | --- | --- |
| `jetbrains_mono/` | [JetBrains Mono](https://github.com/JetBrains/JetBrainsMono), drawn at Light (300) weight; the current display font | 2020 The JetBrains Mono Project Authors |
| `noto_sans_mono/` | [Noto Sans Mono](https://github.com/notofonts/latin-greek-cyrillic) Regular | 2015 Google LLC |

To regenerate `font_ui.c/h` from a font, run the `make_font.py` in its folder (this needs Pillow and fontTools). The header of `font_ui.c` names the font in use and its licence file.
