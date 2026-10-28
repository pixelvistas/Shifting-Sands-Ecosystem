#!/bin/sh
# Builds the headless harness against the real src/Games/SuccessionModel.cpp.
# Run: ./succession_harness <outdir> [repoRoot]   (SUCC_ONLY=T1,T3 to run a subset)
set -e
cd "$(dirname "$0")"
g++ -O2 -std=c++14 -Wall -Wextra -o succession_harness main.cpp ../../src/Games/SuccessionModel.cpp
