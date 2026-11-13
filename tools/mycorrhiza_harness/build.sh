#!/bin/sh
# Builds the Mycorrhiza prototype harness (plain C++, no openFrameworks).
cd "$(dirname "$0")" && g++ -O2 -std=c++14 -Wall -Wextra -pthread -o mycorrhiza_harness main.cpp \
  ../../src/Games/SuccessionModel.cpp ../../src/Prototypes/Mycorrhiza/MycorrhizaField.cpp
