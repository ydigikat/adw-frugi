# ------------------------------------------------------------------------------
#  MIT License - Copyright (c) 2026 YDigiKat
# ------------------------------------------------------------------------------
#
# ST Arm Clang, "Hybrid": Clang compiles and assembles, but linking
# and runtime (newlib-nano, crt*.o, libgcc) still come from the bundled GNU
# toolchain. See <STM32CUBECLT_PATH>/st-arm-clang/doc/starm-clang.md.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Locate toolchain via environment variable
if(NOT DEFINED ENV{STM32CUBECLT_PATH})
    message(FATAL_ERROR
        "STM32CUBECLT_PATH is not set.\n"
        "Export this environment variable to point to your STM32CubeCLT installation."
    )
endif()

file(TO_CMAKE_PATH "$ENV{STM32CUBECLT_PATH}" STM32CUBECLT_ROOT)
set(TOOLS_DIR "${STM32CUBECLT_ROOT}/st-arm-clang/bin/")

if(NOT EXISTS "${TOOLS_DIR}starm-clang")
    message(FATAL_ERROR
        "clang not found under STM32CUBECLT_PATH:\n"
        "  ${STM32CUBECLT_ROOT}"
    )
endif()

message(STATUS "STM32CubeCLT found at ${STM32CUBECLT_ROOT}")

# Compiler and tool paths
set(CMAKE_C_COMPILER   ${TOOLS_DIR}starm-clang)
set(CMAKE_ASM_COMPILER ${TOOLS_DIR}starm-clang)
set(CMAKE_CXX_COMPILER ${TOOLS_DIR}starm-clang++)
set(CMAKE_OBJCOPY      ${TOOLS_DIR}starm-objcopy)
set(CMAKE_SIZE         ${TOOLS_DIR}starm-size)

# Hybrid toolchain option (not full LLVM) so depending on GNU linker
# artefacts/crt0 etc.
set(STARM_GCC_TOOLCHAIN "${STM32CUBECLT_ROOT}/GNU-tools-for-STM32")
set(STARM_MULTILIB_CONFIG "${STM32CUBECLT_ROOT}/st-arm-clang/multilib.gnu_tools_for_stm32.yaml")

set(CMAKE_C_FLAGS   "-fdata-sections -ffunction-sections -fstack-usage --std=c23 --gcc-toolchain=${STARM_GCC_TOOLCHAIN} --multi-lib-config=${STARM_MULTILIB_CONFIG}")
set(CMAKE_ASM_FLAGS "-x assembler-with-cpp")
set(CMAKE_C_FLAGS_DEBUG   "-Og -g3")
set(CMAKE_C_FLAGS_RELEASE "-O3 -ffast-math")

set(CMAKE_EXE_LINKER_FLAGS "--gcc-specs=nano.specs -Wl,--gc-sections -Wl,-z,noexecstack -u _printf_float")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
