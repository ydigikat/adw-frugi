# ------------------------------------------------------------------------------
#  MIT License
#  Copyright (c) 2026 YDigiKat
#
#  Permission to use, copy, modify, and/or distribute this code for any purpose
#  with or without fee is hereby granted, provided the above copyright notice
#  and this permission notice appear in all copies.
# ------------------------------------------------------------------------------
# Host toolchain (clang) - only used to build and run the unit tests.

# Only applies when we're building for the unit test on Linux
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_C_COMPILER clang-23)
set(CMAKE_C_FLAGS "-O0 -std=c2x -g3 -Wall -Wextra -fprofile-arcs -ftest-coverage -DDEBUG")