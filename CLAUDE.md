# Project notes for Claude

Current state only. History, reversed decisions and test results live in
`docs/DEVLOG.md` (the full pre-2026-10-28 CLAUDE.md is archived there,
verbatim). Add new decisions and results to the DEVLOG, not here; keep this
file under 150 lines and update it only when the current state changes.

## Project

Shifting Sands: an AR sandbox (Kinect v1 + projector) forked from Magic
Sand, ported to openFrameworks 0.12.1 with an ofxImGui GUI (`README.md`,
`PATCHES.md`). It is an MSc/MRP thesis project; the build freezes on
2026-11-02.

## Branches

- `ss/succession-feedback` (current work): vegetation-to-site feedback
  succession model on top of `ss/dewitz-flow`.
- `ss/dewitz-flow`: Dewitz-style flow particles, water table.
- `ss/terrain-reference-visuals`: parent of `ss/dewitz-flow`; stock height
  colour map plus simulated water.
- `ss/ecosim-model-visuals-kiqw12`: fallback; full ELF species rendering
  in the shader (no further work there); its species colours were ported to
  `ss/succession-feedback`.
- Commit `1bbad8a`: last hardware-confirmed ELF port before the
  succession-model work.

## Frame order (`src/ofApp.cpp`)

`kinectProjector->update()`, `sandSurfaceRenderer->update()`,
`vegetationField.update()` (vegetation, then `stepFlow()`),
`critterController.update()`, `waterFlowField.update()`. Drawing:
sand surface (shader), flow particles, agents, then the ImGui panels.

## Classes

- `src/KinectProjector/*`: Kinect capture, temporal filtering, projector
  calibration; `elevationAtKinectCoord()`, `kinectCoordToProjCoord()`.
- `src/SandSurfaceRenderer/*`: draws the sand with
  `bin/data/shaders/shadersGL{2,3}/heightMapShader.{vert,frag}` (keep the
  two variants in sync). Colour comes from
  `bin/data/colorMaps/HeightColorMap.xml` (key height in mm = elevation
  in mm). Keys run -45..85 mm, rescaled to this box's measured dig/mound
  limits (-41.8/+80 mm); below -6 mm they are five dry earth tones, so
  blue on the sand means simulated water only; sage #B4BC92 runs -6..14.2 mm
  (flat sand sits inside it), then five even bands to 85 mm (..#B9561A). The map is stepped, not blended: each band is a pair
  of keys with the same colour (start, next start - 0.01 mm). The shader adds the simulated-water
  overlay (vegetation texture alpha 1..249 = depth) and contour lines. On
  `ss/succession-feedback` it also draws vegetation (dark #3B3A22 map
  symbols, see `roleSymbol()`) and the soil tint.
- `src/Games/VegetationField.*`: one grid cell per Kinect pixel in the ROI
  (~519 x 295). Owns:
  - ELF vegetation: shrub/fruit/nut densities (0..1), ELF's literal
    per-tick growth chances (1/3/2 %) inside elevation bands set by
    `TEMPERATURE` and `LIVING_RANGE_FRACTION`, plus same-species spread
    (`hasEstablishedNeighbor`, `SPREAD_RADIUS_MM`).
  - Temperature: `BASE_TEMPERATURE` (0.745) eased upward by sculpting
    activity (`ACTIVITY_TO_TEMPERATURE`, `MAX_TEMPERATURE_OFFSET`).
  - Hydrology (`stepFlow()`): see below.
  - `combinedTex` (RGBA8): R/G/B = shrub/fruit/nut, A = water depth
    (1..249) or snow (255).
  - Queries: `isWaterCell`, `isWaterAt`, `getWaterDepthAt`,
    `getFlowVelocityAt`, `getDepthBelowWaterTableAt`, `getElevationGrid`,
    `getMmPerCell`; agent food: `eatShrubOrFruit`, `eatFruitOrNut`.
  - Owns an `EcologyParams` (shown in the Vegetation panel).
- `src/Games/EcologyParams.*`: loads the designer or published preset
  (`bin/data/settings/ecologyParams{Designer,Published}.xml`): per role
  (pioneer=fruit, intermediate=nut, climax=shrub) moisture, slope and soil
  depth ranges with units and sources. The published preset is still
  TODO placeholders (zeros).
- `src/Games/CritterController.*`, `Critter.*`, `HumanAgent.*`: ELF deer
  and human agents (spawn counts default to 0 on this branch line; use
  the Population panel).
- `src/Games/WaterFlowField.*`: visible flow particles. Default mode
  "Dewitz (2018)" uses `DewitzFlow`; older modes (bed slope, pipe-model
  velocity, `LatticeFlow`) stay selectable. Particles default to "Water
  only" (spawn near the inlet on `isWaterCell` cells, fade out over 0.5 s
  on land). Optional obstacle-mask overlay.
- `src/Games/DewitzFlow.*` (plain C++, no openFrameworks): whole-box D2Q9
  MRT lattice-Boltzmann (tau 1.98), obstacles from height/slope with
  hysteresis, downhill pull, attractors; purely visual.
- `src/Games/LatticeFlow.*`: earlier LBM-inside-the-water mode; kept, not
  default.
- `src/Games/SuccessionModel.*` (plain C++; branch `ss/succession-feedback`):
  vegetation with site feedback (thesis Eq. 2.2). Per-cell fields: cover of
  pioneer/intermediate/climax (0..1, total <= 1), moisture M and soil S
  (0..1). Ranges come from `EcologyParams` (refuses unknown units and 0/0
  placeholders); rates are statics at the top of `SuccessionModel.cpp`,
  live under Vegetation panel -> Succession. `VegetationField` owns it;
  "Vegetation model" switches ELF bands (Eq. 2.1) / Site feedback (default).
  Updates every 3rd row per frame (`UPDATE_INTERLEAVE`). Shader: role colours
  as dark map symbols (pioneer small dots, intermediate crosses, climax
  larger dots; density follows cover), soil tint, M/S debug overlay. Headless tests:
  `tools/succession_harness/` (T1-T7, `plot.py`).

## Hydrology (`VegetationField::stepFlow()`)

Virtual-pipe shallow water (Mei, Decaudin & Hu 2007), 2 substeps per
frame, over terrain smoothed with `FLOW_TERRAIN_SMOOTHING_SEC`. Sources:
edge inflow (`SOURCE_EDGE`, `SOURCE_SPAN`, `INFLOW_MM_PER_SEC`) and a
water table (`WATER_TABLE_MM` = -15, `SEEPAGE_MM_PER_SEC` = 4) that fills
basins dug below it. Sink: `SINK_EDGE` (open boundary). Water is a cell
whose depth passes `MIN_VISIBLE_WATER_DEPTH_FRAC` with hysteresis
(`WATER_HYSTERESIS_FRAC`). `USE_FLAT_SEA` (legacy ELF sea) is off.
Known: water above the table has no infiltration, so a refilled hole
can leave a sub-visible film; basins settle ~1 mm above the table.

## Parameters

All tunables are `static` members defined at the top of each class's
`.cpp` and exposed live in the ImGui panels (Vegetation, Water Flow,
Population, Kinect/Sand). Change the default in the `.cpp`; do not
duplicate values elsewhere.

## Build and test

- Real build: Visual Studio, `Magic-Sand-Ecosystem.sln`, **Release**
  configuration (Debug runs at ~5 fps). The `.vcxproj` and
  `.vcxproj.filters` list source files explicitly: add every new file to
  both.
- The development session has no openFrameworks SDK. Only plain-C++
  classes can be compiled there. Headless harnesses:
  - `tools/dewitz_harness/build.sh` (compiles `DewitzFlow.cpp`; reference
    prototype `tools/dewitz_proto.py`).
  - For `stepFlow()`, the function text was extracted and compiled
    against a stub class in the scratchpad (see the DEVLOG, 2026-10-22).
- Everything else is verified only on the sandbox by the user. Say so
  plainly when reporting: "not compiled with openFrameworks, not
  hardware-tested".

## Working rules

- History and decisions go in `docs/DEVLOG.md`.
- On every hardware report, check first: correct branch and `git pull`,
  Release build, and the Vegetation panel's elevation range reset to the
  calibrated ceiling ("Reset range to ±calibrated ceiling"), a recurring
  cause of wrong water/snow lines.
- Measure before tuning: use the GUI readouts rather than adjusting by eye.
- Avoid `near`/`far` as identifiers (Windows macros).
- Accessibility: avoid saturated red next to saturated green.
