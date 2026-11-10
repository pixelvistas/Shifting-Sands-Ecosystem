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

- `ss/plant-grain` (current work): grain-dither plants and seed particles
  (display only), on `ss/land-lines`.
- `ss/land-lines`: land lines, band fill, flat fade, Kinect driver stack; on `ss/water-lines`.
- `ss/water-lines` (water lines) on `ss/succession-feedback` (succession) on `ss/dewitz-flow`.
- `ss/dewitz-flow`: flow particles, water table. Fallback `ss/ecosim-model-visuals-kiqw12`.

## Frame order (`src/ofApp.cpp`)

`kinectProjector->update()`, `sandSurfaceRenderer->update()`,
`vegetationField.update()` (vegetation, then `stepFlow()`),
`critterController.update()`, `waterFlowField.update()`. Drawing:
sand surface (shader), flow particles, agents, then the ImGui panels.

## Classes

- `src/KinectProjector/*`: Kinect capture, temporal filtering, calibration.
- `src/SandSurfaceRenderer/*`: draws the sand with
  `bin/data/shaders/shadersGL{2,3}/heightMapShader.{vert,frag}` (keep the
  two variants in sync). Colour comes from
  `bin/data/colorMaps/HeightColorMap.xml` (key height in mm = elevation
  in mm). Keys run -45..85 mm, rescaled to this box's measured dig/mound
  limits (-41.8/+80 mm); below -6 mm five darker greens (#7DAF76 ..
  #376235 at the deepest), so blue on the sand means simulated water only;
  land: green #94BF8B -6..20 mm (flat sand ~9 mm), then yellow, tan, brown
  in 16 mm bands, white #F5F4F2 68..85 mm. LOCKED until after 2026-11-27
  (committee submission). The Vegetation panel's swatch hard-codes these
  colours (keep in sync). Stepped: each band is a key pair (start, next
  start - 0.01 mm). The shader adds the simulated-water
  overlay (vegetation texture alpha 1..249 = depth), drawn over a plain
  lakebed #B8B0A0 (no land colour, no contours under water), and contour
  lines elsewhere; also soil tint, plants (below), M/S debug overlay, water lines
  (glowing #6FD3FF bed isolines on simulated water, + shoreline glow; Sand Surface
  Renderer panel, `sandSurfaceRendererSettings.xml`, `tools/water_lines_preview.py`).
  `ss/land-lines` adds glowing land lines: band edges (from the map's keys)
  outline each band in its own colour, the shore too (land side; water side blue,
  both on); every N mm in the band at the level. Fill on: land lines mixed in a deepened
  colour ("Line shade on fill"); off, and water lines always: additive glow. "Band fill" off for a
  black dark-room base, and "Hide lines on flat ground" (slope fitted to 8
  filtered-depth samples at +-5 Kinect px; default 0.6, `tools/land_lines_preview.py`).
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
  - Queries: `isWaterCell`, `getWaterDepthAt`, `getFlowVelocityAt`,
    `getDepthBelowWaterTableAt`, `getElevationGrid`; food: `eat*`.
  - Owns an `EcologyParams` (shown in the Vegetation panel).
- `src/Games/EcologyParams.*`: designer/published preset (`bin/data/settings/
  ecologyParams*.xml`): per role (pioneer=fruit, intermediate=nut, climax=shrub)
  moisture, slope, soil depth ranges. Published preset: still TODO zeros.
- `src/Games/CritterController.*`, `Critter.*`, `HumanAgent.*`: ELF deer and
  humans (spawn counts default to 0; Population panel).
- `src/Games/WaterFlowField.*`: visible flow particles. Default mode
  "Dewitz (2018)" uses `DewitzFlow`; older modes (bed slope, pipe-model
  velocity, `LatticeFlow`: earlier LBM-in-the-water) stay selectable. Particles default to "Water
  only": they move with the pipe model's velocity (`getFlowVelocityAt`,
  smoothed; `DewitzFlow::setPipeVelocity`), spawn on `isWaterCell` cells
  weighted by local speed, fade out on land (0.5 s) and in still water
  (after 1 s, over 1 s). "Everywhere" keeps the original Dewitz rule.
  Optional obstacle-mask overlay.
- `src/Games/DewitzFlow.*` (plain C++, no openFrameworks): whole-box D2Q9
  MRT lattice-Boltzmann (tau 1.98), obstacles from height/slope with
  hysteresis, downhill pull (default 0), attractors; purely visual. In
  "Water only" the lattice is only a small texture term (`W_LBM_TEXTURE`).
  Headless test with the real `stepFlow()`: `tools/pipe_particles_harness/`.
- `src/Games/SuccessionModel.*` (plain C++; branch `ss/succession-feedback`):
  vegetation with site feedback (thesis Eq. 2.2). Per-cell fields: cover of
  pioneer/intermediate/climax (0..1, total <= 1), moisture M and soil S
  (0..1). Ranges come from `EcologyParams` (refuses unknown units and 0/0
  placeholders); rates are statics at the top of `SuccessionModel.cpp`,
  live under Vegetation panel -> Succession. `VegetationField` owns it;
  "Vegetation model" switches ELF bands (Eq. 2.1) / Site feedback (default).
  Updates every 3rd row per frame (`UPDATE_INTERLEAVE`). Headless tests:
  `tools/succession_harness/` (T1-T7, `plot.py`).
- `src/Games/PlantLayer.*` (plain C++): "Plant display" Discs / Grain dither
  (default) / Off. Discs: dominant role per jittered site (15 mm), eased; state
  texel per site, drawn by the shader in mm (`tools/plant_harness/`). Grain: the
  shader's Bayer 4x4 dither in projector px (density = cover; pioneer 1 px,
  intermediate 2, climax 4, x scale; grey to role colour by soil S).
- `src/Games/SeedParticles.*` (plain C++) + `SeedLayer.*` (FBO, additive, as the
  water particles): seeds where cover grew over a 0.5 s window closed on whole
  multiples of UPDATE_INTERLEAVE (no striping); wind + curl noise + drift from
  the front. Test: `tools/grain_seed_harness/`.

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
- Kinect: libfreenect 0.7.5 + libusb 1.0.30 via `tools/drivers/`, camera only; Windows
  passed 2026-11-07 (`docs/DRIVER_TEST.md`); fallback `KinectDriverStack=fallback`.
- The development session has no openFrameworks SDK. Only plain-C++
  classes can be compiled there. Headless harnesses:
  - `tools/dewitz_harness/build.sh` (`DewitzFlow.cpp`; prototype `tools/dewitz_proto.py`).
  - `stepFlow()`: function text compiled against a stub class (DEVLOG, 2026-10-22).
- Everything else is verified only on the sandbox by the user. Say so
  plainly when reporting: "not compiled with openFrameworks, not
  hardware-tested".

## Working rules

- On every hardware report, check first: correct branch and `git pull`,
  Release build, and the Vegetation panel's elevation range reset to the
  calibrated ceiling ("Reset range to ±calibrated ceiling"), a recurring
  cause of wrong water/snow lines.
- Measure before tuning (GUI readouts). Avoid `near`/`far` identifiers (Windows macros).
- Accessibility: avoid saturated red next to saturated green.
