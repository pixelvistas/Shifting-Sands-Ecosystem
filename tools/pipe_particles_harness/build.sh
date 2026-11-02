#!/bin/sh
# Headless test of the pipe-velocity flow particles: real stepFlow() (copied
# by extract.py) + real DewitzFlow.cpp. Run: ./pipe_particles_harness <outdir>
set -e
cd "$(dirname "$0")"
python3 extract.py
g++ -O2 -std=c++14 -Wall -Wextra -o pipe_particles_harness main.cpp ../../src/Games/DewitzFlow.cpp
