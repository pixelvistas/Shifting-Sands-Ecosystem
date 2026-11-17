#!/bin/sh
# Builds libecosim.so (real stepFlow + real SuccessionModel) for
# scene_preview.py identity. Needs g++ and python3.
set -e
cd "$(dirname "$0")"
python3 ../pipe_particles_harness/extract.py
g++ -O2 -std=c++14 -Wall -Wextra -shared -fPIC -o libecosim.so ecosim.cpp ../../src/Games/SuccessionModel.cpp
