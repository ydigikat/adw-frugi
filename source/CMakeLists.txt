# ------------------------------------------------------------------------------
#  MIT License
#  Copyright (c) 2026 YDigiKat
#
#  Permission to use, copy, modify, and/or distribute this code for any purpose
#  with or without fee is hereby granted, provided the above copyright notice
#  and this permission notice appear in all copies.
# ------------------------------------------------------------------------------

# This is for the ARM cross compiler not native Linux GCC.

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(TARGET app.elf)

  set(SRC_DIR ${PROJECT_SOURCE_DIR}/source)
  set(APP_DIR ${SRC_DIR}/app)
  set(HW_DIR ${SRC_DIR}/hardware)
  set(BSP_DIR ${HW_DIR}/bsp)
  set(IO_DIR ${HW_DIR}/io)
  set(DRIVER_DIR ${SRC_DIR}/drivers)

  # Segger RTT tracing and asserts - only usable with a J-Link probe.  Turn
  # off for other probes, e.g. cmake --preset debug -DRTT_ENABLED=OFF
  option(RTT_ENABLED "Segger RTT tracing (J-Link probes only)" ON)

  # Headless build - the synth played and programmed entirely over MIDI, with
  # no UI (LCD, encoder, buttons) and no patch storage (EEPROM).  This is the
  # original Frugi design and all that's needed to follow the control-plane
  # articles.  Program Change then always loads the default patch.
  #   e.g. cmake --preset debug-headless, or cmake --preset debug -DHEADLESS=ON
  option(HEADLESS "Build without UI and patch storage (MIDI only)" OFF)


  # ------------------------------------------------------------------------------
  # Application
  # ------------------------------------------------------------------------------
  set(SRCS_APP
    # Entry point
    ${APP_DIR}/main.c

    # Audio/MIDI routines
    ${APP_DIR}/midi/midi_parser.c
    ${APP_DIR}/audio/audio.c
    ${APP_DIR}/midi/midi.c

    # Synthesiser control plane code
    ${APP_DIR}/synth/control/keys.c    
    ${APP_DIR}/synth/control/param.c
    ${APP_DIR}/synth/control/map.c
    ${APP_DIR}/synth/control/patch.c
    ${APP_DIR}/synth/control/synth.c
    ${APP_DIR}/synth/control/assign_poly.c
    ${APP_DIR}/synth/control/assign_mono.c
    ${APP_DIR}/synth/control/voice.c
    ${APP_DIR}/synth/control/mod.c

    # Synthesiser DSP code
    ${APP_DIR}/synth/dsp/chorus.c
    ${APP_DIR}/synth/dsp/dca.c
    ${APP_DIR}/synth/dsp/dcf.c
    ${APP_DIR}/synth/dsp/dco.c
    ${APP_DIR}/synth/dsp/env.c
    ${APP_DIR}/synth/dsp/lfo.c
    ${APP_DIR}/synth/dsp/noise.c
    ${APP_DIR}/synth/dsp/shape.c
  )

  # UI and patch persistence - left out of a headless build
  set(SRCS_APP_UI
    # UI code
    ${APP_DIR}/ui/ui.c
    ${APP_DIR}/ui/display.c
    ${APP_DIR}/ui/btn_gesture.c
    ${APP_DIR}/ui/font/font_ui.c
    ${APP_DIR}/ui/menu/ring_menu.c
    ${APP_DIR}/ui/menu/modal_menu.c

    # Patch persistence
    ${APP_DIR}/patch/patch_store.c
    ${APP_DIR}/patch/patch_persist.c
  )

  set(INCL_APP
    ${APP_DIR}
    ${APP_DIR}/patch
    ${APP_DIR}/ui
    ${APP_DIR}/ui/menu
    ${APP_DIR}/synth/control
    ${APP_DIR}/synth/dsp
    ${APP_DIR}/audio
    ${APP_DIR}/midi
  )


  set(DEFS_APP
    # Set DEBUG flag for debug presets
    $<$<CONFIG:DEBUG>: DEBUG>

    # Heartbeat strobe on/off
    STROBE_ENABLED

    # Independent watchdog - release only (would reset at debug breakpoints)
    $<$<CONFIG:RELEASE>: WATCHDOG_ENABLED>

    # Synth params, 
    $<$<CONFIG:DEBUG>: SYNTH_MAX_VOICES=6>
    $<$<CONFIG:RELEASE>: SYNTH_MAX_VOICES=8>
    FILTER_DRIVE

    # Audio routine params
    AUDIO_BLOCK_SIZE=128u
    AUDIO_SAMPLE_RATE=48000u
    #$<$<CONFIG:DEBUG>: AUDIO_SANITISE_BUFFER>
    # AUDIO_TEST_TONE

    # MIDI byte buffering
    MIDI_OVERRUN_LEVEL=96u
    $<$<CONFIG:DEBUG>: MIDI_REPORT_OVERRUN>
  )

  # Enable per-module RTT tracing for use with J-Link probes.  These can cost
  # a lot in compute, especially if logging floats and in a tight loop.
  # These do not enable/disable the RTT_ASSERT calls.
  if(${RTT_ENABLED})
    set(TRACE_FLAGS
      # TRACE_BSP          
      # TRACE_AUDIO   
      # TRACE_MIDI 
      # TRACE_UI 
      # TRACE_LEVELS
      # TRACE_SYNTH  
      # TRACE_KEYS
      # TRACE_VOICE
      # TRACE_ASSIGN      
      # TRACE_DCO
      # TRACE_DCF
      # TRACE_DCA
      # TRACE_ENV
      # TRACE_LFO
    )
  endif()


  # ------------------------------------------------------------------------------
  # Board Support Package (BSP) MCU/board bring-up: clocks, FPU etc.
  # ------------------------------------------------------------------------------
  set(SRCS_BSP
    ${BSP_DIR}/startup.s
    ${BSP_DIR}/system_stm32f4xx.c
    ${BSP_DIR}/board.c
  )

  set(INCL_BSP ${BSP_DIR})
  set(DEFS_BSP BSP_ENABLE_LA_PINS)
  set(LINKER_SCRIPT linker_script.ld)

  # ------------------------------------------------------------------------------
  # IO hardware - peripherals and others.
  # ------------------------------------------------------------------------------
  set(SRCS_IO
    ${IO_DIR}/i2s.c
    ${IO_DIR}/midi_uart.c
  )

  # The UI/EEPROM peripherals (all on SPI1 apart from the controls) - left
  # out of a headless build
  set(SRCS_IO_UI
    ${IO_DIR}/spi1_bus.c
    ${IO_DIR}/lcd.c
    ${IO_DIR}/eeprom.c
    ${IO_DIR}/encoder.c
    ${IO_DIR}/buttons.c
  )

  set(INCL_IO ${IO_DIR})
  # 128 bytes covers ~40ms at full MIDI wire rate - headroom for the
  # intentional ~20ms superloop stall during a patch save.
  set(DEFS_IO MIDI_DMA_BUFFER_SIZE=128)

  # ------------------------------------------------------------------------------
  # Drivers & Middleware
  # ------------------------------------------------------------------------------
  set(SRCS_DRIVERS
    ${DRIVER_DIR}/ST/Src/stm32f4xx_ll_rcc.c
    ${DRIVER_DIR}/ST/Src/stm32f4xx_ll_gpio.c
    ${DRIVER_DIR}/ST/Src/stm32f4xx_ll_usart.c
    ${DRIVER_DIR}/ST/Src/stm32f4xx_ll_utils.c
    ${DRIVER_DIR}/ST/Src/stm32f4xx_ll_spi.c
    ${DRIVER_DIR}/ST/Src/stm32f4xx_ll_dma.c
    ${DRIVER_DIR}/ST/Src/stm32f4xx_ll_tim.c

    # Nanolib dependencies
    ${DRIVER_DIR}/sysmem.c
    ${DRIVER_DIR}/syscalls.c
  )

  set(INCL_DRIVERS
    ${DRIVER_DIR}/ST/Inc
    ${DRIVER_DIR}/CMSIS/Device/ST/STM32F4xx/Include
    ${DRIVER_DIR}/CMSIS/Include
  )

  set(DEFS_DRIVERS USE_FULL_LL_DRIVER HSE_VALUE=25000000 STM32F411xE)

  # ------------------------------------------------------------------------------
  # Compiler/Linker flags
  # ------------------------------------------------------------------------------
  set(CFLAGS
    -mcpu=cortex-m4
    -mthumb
    -mfpu=fpv4-sp-d16
    -mfloat-abi=hard
  )

  set(LFLAGS
    -mcpu=cortex-m4
    -mthumb
    -mfpu=fpv4-sp-d16
    -mfloat-abi=hard
  )

  # Compiler warnings
  set(WARNINGS
    -Wall
    -Wextra
    -Werror
    -Wdouble-promotion
    -Wpointer-arith

    # GCC-only: no Clang equivalent, so only apply it for that compiler.
    $<$<C_COMPILER_ID:GNU>:-Wunsuffixed-float-constants>

    # Turn of warnings that occur in STM framework code.  Not fixing those.
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-unused-but-set-variable
    -Wno-unused-value
    -Wno-unused-label
    -Wno-unused-local-typedefs
  )


  # Segger RTT - only useful with J-Link probes.
  if(${RTT_ENABLED})
    list(APPEND SRCS_DRIVERS
      ${DRIVER_DIR}/Segger/RTT/RTT/SEGGER_RTT_printf.c
      ${DRIVER_DIR}/Segger/RTT/RTT/SEGGER_RTT.c
      ${DRIVER_DIR}/Segger/RTT/RTT/SEGGER_RTT_ASM_ARMv7M.S
      ${DRIVER_DIR}/Segger/RTT/Syscalls/SEGGER_RTT_Syscalls_GCC.c

    )
    list(APPEND INCL_DRIVERS
      ${DRIVER_DIR}/Segger/RTT/RTT
      ${DRIVER_DIR}/Segger/RTT/Config
    )
    list(APPEND DEFS_DRIVERS RTT_ENABLED)
  endif()


  # ------------------------------------------------------------------------------
  # Headless or full build
  # ------------------------------------------------------------------------------
  if(${HEADLESS})
    list(APPEND DEFS_APP HEADLESS)
    message(STATUS "Frugi: headless build (no UI, no patch storage)")
  else()
    list(APPEND SRCS_APP ${SRCS_APP_UI})
    list(APPEND SRCS_IO ${SRCS_IO_UI})
  endif()

  # ------------------------------------------------------------------------------
  # Build targets
  # ------------------------------------------------------------------------------
  add_executable(${TARGET} ${SRCS_APP} ${SRCS_BSP} ${SRCS_IO} ${SRCS_DRIVERS})
  target_include_directories(${TARGET} PRIVATE ${INCL_APP} ${INCL_BSP} ${INCL_IO} ${INCL_DRIVERS})
  target_compile_definitions(${TARGET} PRIVATE ${DEFS_APP} ${DEFS_BSP} ${DEFS_IO} ${DEFS_DRIVERS} ${TRACE_FLAGS})
  target_compile_options(${TARGET} PRIVATE ${CFLAGS} ${WARNINGS})
  target_link_options(${TARGET} PRIVATE ${LFLAGS} -T ${BSP_DIR}/${LINKER_SCRIPT})

  # ------------------------------------------------------------------------------
  # Post-build: show binary size
  # ------------------------------------------------------------------------------
  add_custom_command(TARGET ${TARGET} POST_BUILD
    COMMAND ${CMAKE_SIZE} ${TARGET}
  )

endif()
