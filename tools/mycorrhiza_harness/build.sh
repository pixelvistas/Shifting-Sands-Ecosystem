#!/bin/sh
# Builds the Mycorrhiza prototype harnesses (plain C++, no openFrameworks):
#   mycorrhiza_harness  - styles "Patches and hyphae" / "Physarum (previous)" (main.cpp)
#   mycelium_harness    - style "Mycelium" (mycelium.cpp)
cd "$(dirname "$0")" || exit 1
SRC="../../src/Games/SuccessionModel.cpp ../../src/Prototypes/Mycorrhiza/MycorrhizaField.cpp \
  ../../src/Prototypes/Mycorrhiza/MycorrhizaFieldLayers.cpp ../../src/Prototypes/Mycorrhiza/MyceliumSim.cpp"
g++ -O2 -std=c++14 -Wall -Wextra -pthread -o mycorrhiza_harness main.cpp $SRC && \
g++ -O2 -std=c++14 -Wall -Wextra -pthread -o mycelium_harness mycelium.cpp $SRC
