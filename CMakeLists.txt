# ------------------------------------------------------------------------------
#  MIT License
#  Copyright (c) 2026 YDigiKat
#
#  Permission to use, copy, modify, and/or distribute this code for any purpose
#  with or without fee is hereby granted, provided the above copyright notice
#  and this permission notice appear in all copies.
# ------------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.20)

project("Frugi" C ASM)

set(FIRMWARE 1.0.0)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(source)

# Only add tests when building for host
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    enable_testing()
    add_subdirectory("source/test")
endif()
