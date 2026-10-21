# Development log

Decisions and test results. `CLAUDE.md` keeps only a short "Current state";
the history lives here.

## 2026-10-21 - `DewitzFlow`: Dewitz et al. (2018) particle system (branch `ss/dewitz-flow`)

**Task.** Reproduce the flow-particle system of Dewitz et al. (2018), *AR
Sound Sandbox*, Section 3.3, replacing only how the visible flow particles
move and are drawn. `VegetationField::stepFlow()` (virtual-pipe hydrology,
the moisture model) is unchanged.

**Source access.** The paper's full text is not reachable from the
development session (Springer, EUDL, mirevi.de and the HS Duesseldorf
repository fail DNS through the proxy). The implementation follows the
user's written specification of Section 3.3 and the user's reference
prototype `tools/dewitz_proto.py` (BGK, Python). Neither the specification
nor the prototype was checked against the paper text in this session.

**What was built.**
- `src/Games/DewitzFlow.h/.cpp` - standard-library-only class (no
  openFrameworks), so the harness compiles the exact production file.
  - D2Q9 LBM across the whole box, lattice = Kinect ROI grid / `DOWNSAMPLE`
    (default 3 -> 173x98 on this box).
  - Collision: MRT per Lallemand & Luo (2000); moments (rho, e, eps, jx, qx,
    jy, qy, pxx, pxy), shear rate 1/`TAU` (`TAU` = 1.98), s_e = 1.64,
    s_eps = 1.54, s_q = 1.9 (their stability-optimised values). BGK toggle.
  - Inlet edge (selectable) at equilibrium, density 1, `INLET_SPEED` 0.08;
    pressure outlet (density 1, interior neighbour's velocity) on the
    opposite edge; halfway bounce-back on the other two edges and on
    obstacles. Density outside [0.2, 5] or non-finite resets the cell to
    rest and is counted.
  - Obstacles: elevation (block-mean to the lattice, EMA
    `TERRAIN_SMOOTH_SEC`, 3x3 box blur) above `OBSTACLE_HEIGHT_MM` (25) or
    slope above `OBSTACLE_SLOPE` (0.8 rise/run). Raw test fed into an
    evidence EMA (`OBSTACLE_SMOOTH_SEC` 0.3 s) with hysteresis (close > 0.65,
    open < 0.35). A wall that opens starts at equilibrium, density 1, with
    the mean velocity of its fluid neighbours.
  - Downhill term: -grad of the smoothed elevation, magnitude
    min(slope / `GRADIENT_SATURATION`, 1).
  - Attractors: gravity-like min(1, R^2/d^2) toward each point
    (`ATTRACTOR_RADIUS` R), set from the GUI for now.
  - Particles: `dir = W_FLOW*u/|u|max + W_GRAD*downhill + W_ATTR*attractors`
    (1.0 / 0.5 / 0.5), `pos += SPEED*dir*dt`. Entering an obstacle, leaving
    the box, or exceeding `PARTICLE_LIFE_SEC` respawns on the inlet edge.
    Count up to 100,000 in the GUI (default 20,000).
- `WaterFlowField`: new `MOTION_DEWITZ` (default; old modes still
  selectable). Points drawn into an `ofFbo` allocated GL_RGB and cleared
  black, faded each frame by a black quad at `DEWITZ_FADE` alpha, then
  composited with `OF_BLENDMODE_ADD`. GL_RGB avoids the alpha erosion an
  RGBA FBO would suffer under the fade quad. Projector coordinates come
  from a lookup rebuilt each frame every 4 fine cells and interpolated per
  particle, instead of one `kinectCoordToProjCoord()` per particle.
- `VegetationField::getMmPerCell()` - read-only accessor, needed to turn
  height differences into slopes.
- `tools/dewitz_proto.py` - the user's reference prototype, committed as is.
- `tools/dewitz_harness/` - headless harness (`build.sh`, `main.cpp`,
  `png.h`, `export_proto_terrain.py`).

**Departures from the written spec, each taken from the prototype.**
1. `PARTICLE_LIFE_SEC` = 15 s. The spec lists respawn on obstacle/exit only;
   the prototype also respawns at age > 600 frames (15 s at 40 fps).
   Without it, particles accumulate indefinitely in pits and dead zones.
2. `SPEED` = 100 fine cells/s. The prototype moves 1.2 cells/frame on a
   240-cell-wide grid (~0.5 % of the width per frame); scaled to the
   519-cell grid at 40 fps that is ~100 cells/s. The first value tried (40)
   let particles die of age before crossing the slow region upstream of a
   ridge.
3. `GRADIENT_SATURATION` = 0.5 rise/run. The prototype saturates the
   downhill term at slope 2 per cell, ~1.17x the peak slope of its mounds;
   its mounds at box scale peak at ~0.44, so ~0.5. The first value tried
   (0.3) saturated a wide ring around every mound.

**Harness results** (5,000 frames each, 1 lattice step/frame, 40 fps dt,
fresh 1.5 mm Gaussian noise per frame on the box terrains; timings on the
4-core cloud container, not the sandbox PC).

| run | resets / non-finite | lattice ms avg | particles ms | exits/s | notes |
|---|---|---|---|---|---|
| flat | 0 / 0 | 1.00 | 0.98 | 2436 | coherence +1.00 |
| mounds_ridge (mounds flanking the approach) | 0 / 0 | 0.93 | 0.96 | 0 | particles stall upstream of the mounds |
| pit | 0 / 0 | 0.95 | 0.95 | 2459 | pit density 1.86x fluid mean |
| proto_layout (prototype layout at box scale) | 0 / 0 | 0.90 | 0.94 | 0 | 100 % of ridge-column particles in the gap |
| flat, gradient only | 0 / 0 | 0.98 | 0.97 | 0 | no transport (see below) |
| mounds_ridge, BGK tau 1.98 | 0 / 0 | 1.05 | 0.96 | 0 | max u 0.117 |
| mounds_ridge, BGK tau 0.55 | 0 / 0 | 1.02 | 0.92 | 0 | max u 0.341, did not blow up |
| prototype terrain, BGK | 0 / 0 | 2.34 | 2.09 | 0 | 240x150 lattice; matches prototype figure |
| prototype terrain, MRT | 0 / 0 | 1.74 | 1.78 | 0 | same |

Worst single-frame lattice times reached 2-20 ms on the shared container;
averages are the meaningful figure. Wall flips: 0-0.6 per frame under
noise (of 1,700-2,100 obstacle cells).

**Comparison with `tools/dewitz_proto.py`.** On the prototype's exact
terrain (exported by `export_proto_terrain.py`, same code and seed) with
the prototype's settings, the C++ reproduces the prototype figure: the
funnel out of the inlet wedge, a single stream through the ridge gap, the
S-curve toward the pit. Flow and downhill terms sampled along that stream
agree with the prototype's to within ~3 % (max u 0.197 vs 0.194).

**Acceptance criteria.**
- No resets/NaN over 5,000 steps at tau 1.98: met on every terrain, MRT
  and BGK.
- Coherent inlet-to-outlet streams on flat noisy ground: met (coherence
  +1.00, 2,436 exits/s). The "dendritic speckle" contrast in the prototype
  uses particles dropped anywhere; with inlet respawn, gradient-only
  particles on flat ground simply do not move (coherence -0.01, 0 exits) -
  the harness contrast is therefore not like-for-like with the prototype's
  third panel.
- Streams split around mounds and pass through the ridge gap: met on the
  prototype layout (100 % of ridge-column particles in the gap), NOT met on
  the harness's own mounds_ridge terrain, where the two mounds flank the
  approach to the gap. There the ridge throttles the whole box: upstream
  flow term u/|u|max is only ~0.06-0.11 (|u|max is set by the jet in the
  gap), while the downhill pull off the mound skirts is ~0.2-0.4 at
  `W_GRAD` 0.5, so particles stall ~50 cells before the mounds. The
  prototype has the same physics and avoids it only through layout.
  Not tuned away; raised with the user, since real sculpted sand will often
  put mounds in the approach.
- Particles collect in the dug pit: met on the pit terrain (1.86x mean
  density; streams converge into it and leave as a concentrated jet). On
  the prototype's own terrain neither implementation collects in the pit:
  the prototype holds 0.22-0.32x mean pit density at steady state, the C++
  ~0 (its stream ends ~10 cells short of the pit centre).
- Lattice update under 2 ms at the default resolution: met on average
  (0.90-1.05 ms at 173x98), on this container.

**Not done / not verified.** `WaterFlowField` integration (FBO, GUI) is not
compiled - no openFrameworks SDK in the session; only `DewitzFlow.cpp` is
compiled and tested. Not hardware-tested. No further visual tuning until
the user has tested on the sandbox.

## 2026-10-21 - Obstacle overlay, line-segment particles, particle colour

At the user's request, before the first sandbox test; no other behaviour changed.

- `DewitzFlow` now records each particle's position at the start of the
  advection step (`particlePrevX/Y`). A respawned particle gets
  prev == current, so its segment has zero length and is skipped, rather
  than drawing a line across the box to the inlet. Bookkeeping only: the
  motion rule and RNG sequence are unchanged (harness still builds clean).
- `WaterFlowField` draws each particle as an `OF_PRIMITIVE_LINES` segment
  from previous to current position into the same black, faded,
  additively composited FBO. Line width is the GL default (1 px).
- "Show obstacle mask" checkbox (off by default) draws the lattice's solid
  cells as alpha-blended quads (default magenta, 35% alpha, adjustable)
  over the terrain, beneath the particles, in both windows.
- The existing point colour/alpha controls are relabelled "Particle colour"
  and "Particle alpha"; defaults unchanged.

Not compiled against openFrameworks (no SDK in this session); not yet
tested on hardware.

## 2026-10-21 - Obstacle mask: base level, slope-test blur, open inlet

At the user's request, before hardware testing; nothing else changed.

- `OBSTACLE_BASE_MM` (default 0 mm, GUI "Slope walls only above"): the
  slope criterion now marks a cell solid only where its (3x3-smoothed)
  elevation is also above this level, so the walls of dug pits and
  channels stay fluid. The height criterion (`OBSTACLE_HEIGHT_MM`) is
  unchanged.
- `OBSTACLE_BLUR_SIGMA` (default 2 lattice cells, ~11 mm; GUI "Slope-test
  blur", 0-6): a separable Gaussian on the time-smoothed elevation, used
  only for the slope test, so Kinect depth terraces do not form striped
  walls. Below 0.5 it falls back to the previous 3x3 field. The downhill
  term and the height/base tests still use the 3x3 field, so particle
  motion is unchanged.
- The lattice column (or row) on the inlet edge is always fluid: its
  obstacle evidence is held at 0, so a wall there opens immediately
  through the existing open-at-equilibrium path.
- The obstacle overlay is unchanged.
