#!/bin/sh
# Builds the headless species tool against the app's own LeniaSpecies.cpp.
set -e
cd "$(dirname "$0")"
g++ -std=c++17 -O2 -Wall -Wextra -o species_tool species_tool.cpp ../../src/Games/LeniaSpecies.cpp
