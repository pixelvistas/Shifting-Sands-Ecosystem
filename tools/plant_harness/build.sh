#!/bin/sh
# Headless check of the plant display: real SuccessionModel.cpp + PlantLayer.cpp.
# Run: ./plant_harness <outdir> [repoRoot]
set -e
cd "$(dirname "$0")"
g++ -O2 -std=c++14 -Wall -Wextra -o plant_harness main.cpp ../../src/Games/SuccessionModel.cpp ../../src/Games/PlantLayer.cpp
