# lenia_lab: headless checks for the Lenia lab

Branch `prototype/6-lenia`. Runs the lab's own GLSL
(`bin/data/shaders/lenia/GL3|GL2`) in the pass order of
`src/Games/LeniaLab.cpp`, on setups written by the app's own
`src/Games/LeniaSpecies.cpp` (compiled here into `species_tool`), and
checks them against independent NumPy references (`reference.py`, written
from Chan's and Plantec's published code as read).

    ./build.sh                                                  # species_tool (g++, C++17)
    ./species_tool list ../../bin/data/lenia/species.json       # every preset loads and assembles
    ./species_tool dump ../../bin/data/lenia/species.json out/setup
    python3 lenia_lab.py verify      # GPU vs NumPy (Lenia FFT update; Flow-Lenia step)
    python3 lenia_lab.py presets --gif   # 22 s per preset at the defaults: size, speed, mass, GIF
    python3 lenia_lab.py two --gif       # two species (tuned worlds)
    python3 lenia_lab.py touch --gif     # the erase brush through an Orbium
    python3 lenia_lab.py edge --gif      # organisms at the mask edge
    python3 lenia_lab.py gradient --gif  # drift with the gradient on
    python3 lenia_lab.py embed --gif     # Flow-Lenia parameter embedding
    python3 lenia_lab.py mass            # 60 s total mass per channel
    python3 lenia_lab.py compare         # GL2 vs GL3
    python3 lenia_lab.py timing          # software GL: not the installation PC

GIFs go to `docs/lenia_lab/`. Lab defaults used by the harness (cell size,
time scale, colours, glow, trail) are copied at the top of `lenia_lab.py`;
keep them in step with `makeDefaults()` in `LeniaLab.cpp`.

Generating the species file (only needed when presets change):

    python3 make_gl2.py                       # GL2 shaders from GL3 (--check: stale?)
    python3 flowlenia_seeds.py                # needs jax; seeded rules of the published examples
    python3 make_species.py <Chan's Python/animals.json> flowlenia_seeds.json \
        flowlenia_search_results.json 1006,1003

`flowlenia_search.py` is the random search over the published Flow-Lenia
rule space whose results (`flowlenia_search_results.json`) give the two
"found by search" rules (tuned, not published).

Self-contained (EGL, Mesa llvmpipe). Needs numpy, scipy, Pillow, PyOpenGL
and `libegl1 libegl-mesa0 libgl1-mesa-dri`; `flowlenia_seeds.py` needs jax.
