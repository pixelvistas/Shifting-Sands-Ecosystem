#!/bin/sh
# Builds the grain-dither / seed-particle harness (plain C++, no openFrameworks).
cd "$(dirname "$0")" && g++ -O2 -std=c++14 -Wall -Wextra -o grain_seed_harness main.cpp \
  ../../src/Games/SuccessionModel.cpp ../../src/Games/SeedParticles.cpp
