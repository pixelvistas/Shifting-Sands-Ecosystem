#!/bin/sh
# Builds the headless harness against the real src/Games/DewitzFlow.cpp.
set -e
cd "$(dirname "$0")"
g++ -O2 -std=c++14 -Wall -Wextra -o dewitz_harness main.cpp ../../src/Games/DewitzFlow.cpp
