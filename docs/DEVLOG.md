# Development log

Decisions and test results. `CLAUDE.md` describes only the current state;
the history lives here, oldest first.

## Archived CLAUDE.md history (moved here unchanged, 2026-10-28)

Until 2026-10-28 `CLAUDE.md` held the full project history (over 3,000
lines, including decisions that were later reversed). It is reproduced
below verbatim, between the two rules. Entries after the second rule were
written in this file directly.

---

# Project notes for Claude

## Current state

- Branch `ss/dewitz-flow`: visible flow particles use `DewitzFlow` (`src/Games/DewitzFlow.h/.cpp`), a Dewitz et al. (2018) Sec. 3.3 reproduction - whole-box D2Q9 MRT lattice-Boltzmann (tau 1.98), obstacles from hills/steep slopes, downhill pull, GUI attractors.
- It is purely visual; `VegetationField::stepFlow()` (virtual pipes) remains the hydrology/moisture model; sources: edge inflow plus a water table (`WATER_TABLE_MM`, seepage into dug basins).
- Parameters: static members at the top of `DewitzFlow.cpp`; drawing parameters (`DEWITZ_*`) in `WaterFlowField.cpp`; all live in the "Water Flow" panel (mode "Dewitz (2018)").
- Particles default to "Water only": drawn on `VegetationField::isWaterCell` (simulated water); colour map below 0 mm is dry earth, so blue = simulated water.
- Headless test: `tools/dewitz_harness/build.sh`, reference `tools/dewitz_proto.py`; decisions and results in `docs/DEVLOG.md`.

## Reference point: "confirmed working checkpoint" = commit `1bbad8a`

Last commit before the CA-style spread layer / succession-model phase
began (`48cc880` onward). At `1bbad8a`, the ELF port itself was
confirmed working end-to-end on real hardware: full structural fidelity
audit complete, growth/eating/movement/fishing/hunting all verified
against ELF's actual source, and the vegetation pipeline (water/snow/
shrub/fruit/nut) confirmed correctly classifying and rendering on real
hardware. Use this as the point to compare against or `git reset`/
`git checkout` back to if the succession-model work needs reverting.

An annotated git tag `confirmed-working-checkpoint` pointing at this
commit exists locally but could NOT be pushed to origin from this
session (HTTP 403 on tag refs specifically - branch pushes work fine,
so this looks like a scoped-token restriction excluding tags, not a
network issue). If a real pushed tag is wanted, run this from a machine
with full push access:
`git tag -a confirmed-working-checkpoint 1bbad8a -m "..." && git push origin confirmed-working-checkpoint`

## Structural fidelity audit vs. ELF source (2026-09-18)

Full side-by-side re-read of our C++ port against ELF's actual Java
source (`BDenvironment.java`/`BDagent.java`/`BDdeer.java`/`BDlocation.java`
in the ELFdev001/ELFDynamicSystem clone), covering movement, eating,
fishing, hunting, spawn/death/corpse constants, and population caps -
not just the growth mechanic covered above. Confirmed accurate: deer's
permissive-OR movement rule, humans' wait/stutter-step rule, all
water/snow/growth band thresholds, eating gated on `food < MAX_FOOD`,
fishing/hunting chance-equals-skill mechanics, the literal (not "fixed")
`fruittaken + nutstaken/100` gathering formula, and all spawn/death/
corpse constants (`LIFE_OF_CORPSE=500`, `SPAWN_CHANCE_PER_TICK=1.0`,
etc.). Population cap (60/60 vs. ELF's literal `MAXAGENTS/MAXDEERS=1000`)
is a documented, deliberate adaptation for real-time draw cost - already
flagged as intentional in `CritterController.h`, not a new finding.

**Found and fixed (`e94d39f`): update order was backwards.**
`CritterController::update()` updated deer before humans, with a
comment claiming this matched ELF - it didn't. `BDenvironment.step()`
literally calls `stepAgents()` then `stepDeer()` every tick; re-reading
it directly (not trusting the existing comment) confirmed humans go
first in real ELF. Swapped to match: humans now update first, so a
hunt this tick checks each deer's end-of-previous-tick position, same
as ELF, instead of a position the deer moved to later in the same tick.

**Documented, per explicit user instruction, as intentional (not
reverted):** two places where the port is more correct than ELF's own
literal arithmetic rather than reproducing it bug-for-bug:
- Food is clamped at `MAX_FOOD` on eating; ELF's `stepAgents()`/
  `stepDeer()` add with no cap at all (a big tile can overshoot 255).
- Fishing/hunting skill is clamped at 100; ELF's `setFishing()`/
  `setHunting()` have a real bug (unclamped value written to the field
  before a clamp is computed on a local copy that's never written
  back), so skill is actually unbounded in real ELF.

Both are now called out in code comments (`Critter.cpp`/
`HumanAgent.cpp`) as deliberate deviations rather than left silent.

## Follow-up: Repast reference paper (logged 2026-09-18, revisit after ELF port is solid)

User asked whether to adopt Repast Simphony/HPC for the human/deer agents.
Conclusion: **no** — Repast is a Java (Simphony) or MPI-cluster (HPC) toolkit;
adopting it would mean a language/runtime rewrite for no real gain, since our
simpler ELF-derived scheduler/grid already fits this project's actual needs
(single process, real-time, tightly coupled to the Kinect/openFrameworks
render loop).

User then shared a real reference paper: Guest, Bernardes, Howard (2022,
University of Georgia AGU poster) - "Integration of an Agent-Based Model and
Augmented Reality for Immersive Modeling Exploration." It describes a
Repast Simphony + Tangible Landscape (GRASS GIS) + Kinect AR sandbox running
a sheep/wolf/grass predator-prey ABM: DEM captured from sand and fed into
Repast, agent movement constrained by elevation/slope with a slope-based
energy cost, and "Population Over Time" line charts (species counts vs.
tick) as the primary output across four terrain scenarios.

**Do NOT adopt Repast or its stack** - the DEM-ingestion pipeline (GRASS
GIS/Tangible Landscape) is irrelevant since we already read depth natively
via KinectProjector/SandSurfaceRenderer. But the paper validates three
features as worth building natively in our own C++/ImGui, once the ELF
port itself is verified working on hardware:

1. **Population-over-time chart** - ImGui line plot (ImPlot or
   `ImGui::PlotLines`) fed by a ring buffer sampled once per `update()`:
   human/deer counts, vegetation totals per species. Doubles as the
   diagnostic for the still-unconfirmed hypothesis that agents strip
   vegetation faster than it can regrow (see below).
2. **Live agent inspection/probes** - click-to-inspect panel bound to a
   `Critter`/`HumanAgent`'s live fields (position, food/hunger, target,
   state) - equivalent to Repast Simphony's Agent Viewer, no dependency
   needed.
3. **Reproducible logging** - CSV export from the same per-tick sampler
   (tick, counts, RNG seed) - equivalent to Repast's data-set recorders.

**Independent confirmation that #1 isn't just a Repast borrowing:** during
the 2026-09-19 structural audit, `BDdisplay.java` in ELF's own source
turned out to be a 7-line unimplemented stub:

```java
public class BDdisplay {
  //TODO function for displaying output data on top of environment
  //TODO need to add, population data, patches, animals and trees
  //TODO also potentially add some output graphs that update as the
  //     simulation is running
}
```

ELF's own authors planned a population/output-graph display and never
built it - `BDframe`/`BDenvironment` never reference this class at all,
so it's dead scaffolding, not a feature that shipped. The population-
over-time chart above is therefore finishing an ELF design intent left
undone, not just adapting something from the Repast/Tangible Landscape
paper - two independent sources (ELF's own TODO and Guest/Bernardes/
Howard's working implementation) now point at the same feature.

The slope-based movement energy cost from the paper is a plausible design
reference for the *future succession-model phase* (see below), not
something to port now.

**Do not start implementing any of this until the ELF port itself is
confirmed working properly on real hardware** - that's the current
priority per the user's explicit instruction (2026-09-18).

## Future phase: ecological succession model (hydrology + plant seeding, from Liu)

This is the project's stated main goal, not a side idea - in the user's
own words from early in the project: "The main goal is to create an
ecological succession model... What we should have now, is a port of
the ELF model to openFrameworks. What will come after that is something
similar to the Agent-based Simulation from Liu's paper." Confirmed
2026-09-21 that "hydrology model / plant seeding / particle flow" is
this same phase, not a separate idea - this note had compressed that
connection away; restoring it here with the actual source material this
time:

1. **Xun Liu, "The Third Simulation: Augmented Reality Fluvial Modeling
   Tool"** (Journal of Digital Landscape Architecture, 2020) - a
   tangible hydromorphology table combining a physical hydraulic sand
   model with real-time computational fluvial (water-flow) simulation
   via AR: sculpt the sand, see the water-flow simulation update live.
2. A second, longer reference paper (uploaded alongside Liu's,
   `ARsandbox014.pdf`) describing a related system in more depth:
   water-flow visualized through Rhino Grasshopper as vector-field/
   particle-flow imagery, GIS overlays (contour lines, cut-and-fill
   maps), and an agent-based vegetation propagation simulation - a
   hand position on the table reads as a planting/seeding location, and
   how each seed spreads from there is driven by that species' own
   properties and its competitive interaction with whatever other
   species are already established.

So the phase is genuinely three pieces of one methodology, not three
separate features: real-time water-flow simulation + visualization,
plus agent-based seed dispersal/competition running on top of it -
which is exactly what a succession model needs, since succession is
fundamentally about how plant communities shift over time under
hydrology and inter-species competition. Grasshopper itself won't be
used (Rhino-specific, not part of this C++/openFrameworks stack) - it's
the *methodology* being adapted, not the tool.

**Phase started 2026-09-22 - first slice implemented (`VegetationField`'s
spread layer), still needs (a) re-reading both source papers in full
for the actual fluvial simulation math/algorithm Liu used (not just
visual style), and (b) the hydrology/particle-flow water simulation and
real seed-placement mechanics, neither of which exist yet.**

What triggered starting now: real hardware testing (2026-09-22) showed
the vegetation field staying visibly speckled/scarred even after
leaving it undisturbed for several minutes post-population-removal.
Confirmed via direct source reading that this is NOT a bug: ELF's
growth has zero cell-to-neighbor coupling at all (a cell's growth
depends only on its own elevation, shared climate, and a private random
roll - never on what's growing next to it) - i.e., ELF's growth is not
a cellular automaton despite `BDlocation` being called a "cell" (that
word means spatial discretization there, not CA dynamics). Given
expected full-saturation time per cell is ~2.5-7 minutes even
uninterrupted (see the growth-chance math above), and a heavily-grazed
field needs nearly all of many thousands of cells to finish, "still
speckled after a few minutes" is the mathematically expected result,
confirmed by a live density readout ticking up between two checks
(active, just slow) - not evidence of a flaw.

**User's design decision, given this:** rather than just accept ELF's
neighbor-blind pace, add real neighbor coupling on top, as the
deliberate first piece of the succession-model phase (a CA is
literally what a "spread to nearby cells, compete with established
neighbors" model needs - see `CLAUDE.md`'s Liu source material above).
Five concrete decisions made and implemented in `VegetationField.h/.cpp`:

1. **Elevation eligibility stays the gate; neighbor-spread layers on
   top**, not a replacement - a cell still has to be in-band before
   spread can apply.
2. **Neighborhood radius is a real-world mm value** (`SPREAD_RADIUS_MM`,
   default 40mm), not a fixed cell count - converted via a measured
   `mmPerCell` (cached per grid rebuild in `setKinectROI()`, from
   `KinectProjector::kinectCoordToWorldCoord()`), so it means the same
   physical distance regardless of a given installation's Kinect
   resolution.
3. **Empty/eligible cells check outward** each tick
   (`hasEstablishedNeighbor()`), rather than established cells pushing
   into neighbors - cheaper to compose with the existing per-cell loop.
4. **Species competition reuses the existing 1:3:2 growth-chance ratio**
   as the spread weight: a same-species neighbor within radius
   multiplies that species' own `*_GROWTH_CHANCE_PCT` by
   `SPREAD_CHANCE_MULTIPLIER` (default 5x) - since all three species are
   scaled by the same multiplier, their relative 1:3:2 ratio carries
   through unchanged, rather than adding a second, disconnected
   competition parameter.
5. **The plain spontaneous chance (ELF's original mechanic) is kept as
   a fallback** when no established neighbor is found - explicitly
   because the hydrology/particle-flow/seeding layer doesn't exist yet
   to plant real first seeds, so spontaneous growth is the only way to
   bootstrap and test anything on untouched sand until it does.

**RESOLVED (2026-09-22) - the flagged performance risk was real: 60fps
dropped to 1-2fps on real hardware immediately after this layer shipped.**
Confirmed the cause by the numbers before touching anything: up to
`cols*rows*3 species*SPREAD_SAMPLE_COUNT` = ~3.67 million sample
iterations per frame (519x295 grid, 8 samples, 3 species), each doing
2x `ofRandom`, `cos`, `sin`, `sqrt`, and a bounds-checked
`cv::Mat::at<float>()` lookup - more than enough on its own to explain a
30-60x slowdown. Three fixes, in order of actual impact:
1. `hasEstablishedNeighbor()` now uses a raw pointer
   (`density.ptr<float>(0)` + manual row-major indexing) instead of
   `.at<float>()`, which recomputes strides/bounds-checks on every call.
2. `SPREAD_SAMPLE_COUNT` cut from 8 to 3.
3. **The actual main fix:** the neighbor search now only runs on a small
   fraction of eligible ticks (`SPREAD_CHECK_CHANCE=0.1`, i.e. ~10%) -
   the rest just use the plain spontaneous chance, skipping
   `hasEstablishedNeighbor()` entirely. Cutting call volume beats cutting
   cost-per-call, and growth unfolds over minutes regardless, so a cell
   doesn't need its neighbor re-checked every single frame for the
   spread layer to still function. Combined, roughly a ~25-50x reduction
   in the new cost.

**RESOLVED (2026-09-24) - real root cause was the build configuration,
not any code in this file.** Re-tested on hardware after the above fix -
FPS went from 1-2 up to only ~5, not the expected ~60. Checked
`elevationAtKinectCoord()`'s real cost first (a 4x4 matrix-vector
multiply + dot product, ~28 FLOPs/call, ~4.3M FLOPs/frame total across
the grid - too cheap to be the bottleneck on its own), then added
`ENABLE_SPREAD` (`c87caf3`) as an A/B kill switch. User toggled it off on
hardware: **no change, still ~5fps** - which, combined with a direct
`git diff 1bbad8a HEAD --stat` showing only `VegetationField.cpp`/`.h`
touched since the checkpoint at all, ruled out every line of code
changed in this whole phase as the cause (with `ENABLE_SPREAD` off, the
per-cell loop is functionally at least as cheap as the pre-CA baseline -
it even short-circuits once a cell hits max density, which the old code
didn't).

That pointed outside the source tree. `config.make`/`Makefile` have no
optimization overrides (defaults), and the user confirmed they run via
Visual Studio's F5 (`Magic-Sand-Ecosystem.sln`/`.vcxproj` are the actual
build the app runs from, not the Makefile) - F5 builds whichever
configuration the Solution Configuration dropdown is set to, and it was
sitting on **Debug**. Switching to **Release** and rebuilding: FPS went
from ~5-6 to **~40**. Confirmed - this was never a spread-layer cost
problem, it was an unoptimized-build problem that happened to surface
right when a rebuild was first needed to pick up the new C++ code at
all.

**Not fully closed:** 40fps is well short of the ~70fps originally
reported at the pre-CA baseline. Unknown yet whether that baseline
number was itself measured in Release (in which case the spread layer
does carry a real, if much smaller than originally feared, Release-mode
cost worth profiling further) or Debug (in which case 40fps in Release
may already be the honest full-featured number and nothing further is
wrong). Not investigated yet - revisit if 40fps proves limiting in
practice.
`SPREAD_RADIUS_MM=40` and `SPREAD_CHANCE_MULTIPLIER=5` are still
first-guess defaults, untested for whether they look right visually -
now actually testable on hardware for the first time, since the FPS
regression previously made any real observation session impractical.

**Still not done, and blocking full succession-model completion:** the
actual hydrology/particle-flow water simulation and real hand-placed
seeding (vs. the spontaneous-growth stand-in above) - this spread layer
only covers the "compete with established neighbors" half of the
methodology, not the "hydrology model" or "plant seeding" halves.

## Open technical thread: vegetation growth vs. consumption balance

Confirmed by reading ELF's actual source
(`BDenvironment.java`/`BDlocation.java` in the ELFdev001/ELFDynamicSystem
clone):

- **Eating is instant and total in ELF too.** `BDenvironment.java`'s human
  and deer eating loops (~lines 210-221, 300-325) take 100% of whatever
  shrub/fruit/nut density is present at an agent's cell in one step and
  zero it out - no minimum threshold, no partial bite. Our port's
  `eatShrubOrFruit()`/`eatFruitOrNut()` doing the same thing is faithful
  to ELF, not a bug.
- **Growth, however, is NOT continuous in ELF - it's a slow per-tick coin
  flip.** `BDlocation.growShrubs()`/`growFruits()`/`growNuts()` each do a
  `Math.random()*100 < GROWTH` check per tick and, on success, increment
  density by exactly 1 out of a 0..255 max (SHRUBGROWTH=1,
  FRUITGROWTH=3, NUTGROWTH=2 - i.e. 1%/3%/2% chance per tick of +1/255).
  Reaching full density takes on the order of tens of thousands of ticks.
- **RESOLVED (`0afa62f`, 2026-09-18) - reverted to ELF's literal per-tick
  mechanic, per the user's explicit decision.** Was continuous
  (`density += GROWTH_RATE * dt`), an earlier session's unprompted
  smoothness call at the very first vegetation commit (`86b416f`) never
  put to the user as a decision - see git history if the full backstory
  is needed again. Now `VegetationField.cpp`'s `update()` does
  `ofRandom(100.0f) < SHRUB_GROWTH_CHANCE_PCT` (etc., values 1.0/3.0/2.0,
  ELF's literal SHRUBGROWTH/FRUITGROWTH/NUTGROWTH) once per cell per
  frame, incrementing by exactly 1/255 on success - one `update()` call
  is one ELF tick, matching Critter/HumanAgent's existing convention.
  Reasoning given: start from a known-ELF-faithful baseline to diagnose
  the "vegetation never grows" problem from, then customize from there.
  **Important expectation-setting for the next hardware test:** this is
  ELF's actual glacial pace - full density takes on the order of tens of
  thousands of ticks. A short test (seconds, even a minute or two) is
  expected to show little to no visible growth even if everything else
  is working correctly now. Use the pipeline/band diagnostics added in
  `58d1890` (Vegetation panel: stabilized/ROI/gridReady gate states, and
  the ROI-center cell's live band/density readout) to confirm the
  mechanism is actually running and rolling the dice, rather than
  waiting a long time and judging only by whether color appears.
- **RESOLVED - grazing pressure is not the cause.** User ran the proposed
  test (removed all deer/humans via the Population panel, waited) on a
  build before the white->black negative-space change. Result: still
  just blank white, no visible vegetation. With zero agents there is
  nothing left to strip cells bare, so growth itself is not happening at
  any rate - the "~120 agents outrunning regrowth" hypothesis is dead.
  Whatever's wrong is upstream of agents entirely.
- **RESOLVED (2026-09-22) - the full pipeline is confirmed working on
  real hardware.** A real hardware screenshot at Temperature=0.599 shows
  all five classification outcomes simultaneously and correctly: teal
  background (nut-dominant), a light-green ring (shrub-dominant), a
  mottled red/pink patch (fruit-dominant, showing the same per-pixel
  independent-growth speckle predicted from ELF's real per-tick
  mechanic), and a pale-blue patch at a mound's peak matching
  `DEBUG_SHOW_SNOW`'s exact tint `(0.75, 0.85, 1.0)` - confirmed genuine
  snow, not the cosmetic elevation-whitening effect (see below). This
  closes the "is the pipeline even engaging" question for good - it is,
  and all three plant types plus water/snow are all independently
  reachable and visually distinguishable.
- **Root cause of "no water/snow" along the way: the elevation range
  bounds, not Temperature.** Two real debugging rounds on hardware
  (2026-09-22) found the elevation range sliders (bound A/B) had been
  set to an arbitrary ±300mm, more than double the box's own measured
  ceiling (145.7mm per `getCalibratedCeilingElevation()`). Since the
  water/snow lines are fractions OF this range, an oversized range
  makes the derived mm thresholds unreachable no matter how the sand is
  reshaped, and raising Temperature to compensate doesn't fix it since
  Temperature is still just a fraction of the same too-wide range.
  Fixed on the user's hardware by clicking "Reset range to
  ±calibrated ceiling," not a code change.
- **REVERTED (2026-09-24), per explicit user instruction - defaults now
  match ELF's literal values, overriding the two departures above.**
  Real-hardware testing (this session) after the FPS fix showed cranking
  shrub growth way up eventually left the field looking mostly blank/
  black with no reachable water, and the live Vegetation panel readout
  showed Base Temperature had drifted to 0.623 from manual slider use -
  producing a very low snow line (15.9mm) that most of the sand's real
  relief already exceeded. Rather than re-tune sliders live, the user's
  explicit instruction was to fix the CODE DEFAULTS to match ELF first.
  Checked ELF's actual source directly (`BDenvironment.java`) rather
  than trusting this file's own prior paraphrase: `private int
  temperature = 200` and `LIVINGRANGE = 200`, both on ELF's 0..255
  scale - i.e. both 200/255 (~0.784). `TEMPERATURE`/`BASE_TEMPERATURE`
  (previously 0.75, a deliberate post-hardware-testing departure) and
  `LIVING_RANGE_FRACTION` (previously 0.5, same departure) are now both
  `200.0f/255.0f` exactly, matching ELF literally. `SHRUB_LINE_RATIO`/
  `FRUIT_LINE_RATIO`/`NUT_LINE_RATIO` were already exactly ELF's literal
  20/200, 60/200, 25/200 - no change needed there.
  **The previously-documented real-hardware finding this reverts is not
  wrong and is not erased by this change**: ELF's own literal ratio was
  found, on this exact hardware, to leave the water line at the
  calibrated floor and the snow line so high that ordinary hand-
  sculpting couldn't reach either - this revert reintroduces that
  specific risk deliberately, as the user's explicit choice to start
  from a known-ELF-faithful baseline before any further customization,
  not a claim that the old finding no longer applies. If hand-sculpting
  can't reach the water/snow lines at this literal default on real
  hardware, `BASE_TEMPERATURE` is the value to place by hand next (both
  values stay fully live-tunable in the Vegetation panel regardless of
  these code defaults) - see `VegetationField.h`'s header note for ELF's
  own equivalent (`decTemp()`/`incTemp()`, the 'q'/'a' operator keys).
- **REFINED (2026-09-24) - the ELF-literal revert above made water
  exactly zero, confirmed to be ELF's own actual factory-default
  behavior, not a new bug.** User provided a real ELF reference figure
  ("Figure 7: ELF Dynamic System with higher temperatures. Very dark
  blue areas represent areas under water") and asked for water/snow to
  both show up "where it makes sense," pointing straight at why: with
  `TEMPERATURE == LIVING_RANGE_FRACTION` (both ELF's literal 200/255,
  from the revert above), `BDenvironment.stepCells()`'s water condition
  (`height < temperature - LIVINGRANGE`) is literally `height < 0` -
  impossible. Confirmed via direct source read of `incTemp()`/
  `decTemp()` that this is deliberate: ELF's own operator is expected to
  raise temperature above LIVINGRANGE with the 'q' key specifically to
  open a water band - the reference figure's "higher temperatures" is
  exactly that. (Also corrected a direction error in this file's own
  prior comments, which had claimed the operator *lowers* temperature to
  get water via `decTemp()`/'a' - it's `incTemp()`/'q' that raises it;
  re-reading the source directly caught this rather than propagating the
  earlier paraphrase.)
  `TEMPERATURE`/`BASE_TEMPERATURE` moved from `200.0f/255.0f` to `0.9f` -
  still using ELF's own lever (temperature started above
  `LIVING_RANGE_FRACTION`, not a new mechanic), not ELF's literal
  factory-default value, since that value is a documented zero-water
  cold start by ELF's own design. With `LIVING_RANGE_FRACTION` at ELF's
  literal ~0.784, only ~0.216 of the calibrated range is left as slack,
  split between water headroom (`TEMPERATURE - LIVING_RANGE_FRACTION`)
  and snow headroom (`1.0 - TEMPERATURE`) - the same budget, so both
  can't be maximized simultaneously. `0.9` splits it to ~12% water / ~10%
  snow of the total range - both present, neither dominant. First-guess
  starting point per the user's stated goal, untested on real hardware -
  still live-tunable in the Vegetation panel regardless of this code
  default.
- **FIXED (2026-09-24) - random black speckle scattered across otherwise-
  unrelated land, distinct from both contour lines and the confirmed
  water body.** User pushed back on the water/temperature explanation
  above: they weren't asking about contour lines (already understood)
  or the one confirmed water body - they meant scattered black patches
  elsewhere, and pointed at this project's own negative-space design
  decision (`heightMapShader.frag`'s tie rule, untouched land renders
  flat white) asking why these weren't also reading as negative space.
  Traced to a real interaction the negative-space rule never covered:
  that rule only applies to an EXACT all-zero tie - a cell with even one
  successful growth tick (density=1/255) is not a tie, and the shader's
  winner-take-all coloring has no fade (matching ELF's own
  `getCellColor()` exactly - confirmed earlier in the fidelity audit),
  so a channel that wins renders at FULL elevation-modulated saturation
  regardless of how small its density actually is. For shrub `(h,1,h)`
  and fruit `(1,h,h)` that's harmless even at low elevation (one channel
  is always pinned to 1.0, already confirmed looking correct on real
  hardware per the 2026-09-22 pipeline-confirmation note above) - but
  nut's formula, `(0,h,h)`, has no pinned-bright channel, so a single
  lucky nut tick at low elevationNorm renders that cell solid near-black
  instantly, scattered wherever nut's independent per-pixel growth
  happens to land first, indistinguishable by eye from a contour line or
  real water.
  Fix: added `NUT_VISIBILITY_THRESHOLD` (default `0.05`, live-tunable in
  the Vegetation panel) - nut only wins the shader's color comparison
  once its density clears this threshold; below it, the cell falls
  through to the same negative-space default as a real tie. Wired as a
  new `nutVisibilityThreshold` uniform in both `heightMapShader.frag`
  variants (GL2 and GL3, kept in sync per that file's own header note)
  and set from `SandSurfaceRenderer::drawSandbox()`. Deliberately scoped
  to nut only, NOT applied to shrub/fruit - this is a new departure from
  ELF's literal no-threshold `getCellColor()`, motivated purely by this
  visual issue, and touching shrub/fruit's already-confirmed-correct
  instant-speckle behavior wasn't asked for and risked undoing something
  already validated on hardware.
- **Separately identified, not a bug:** a real mound can visually wash
  from red toward white even with the debug-snow toggle off and even
  when nowhere near the actual snow threshold. Traced to
  `SandSurfaceRenderer`'s OWN `elevationNorm` (used only for cosmetic
  color modulation, e.g. fruit's `(1.0, elevationNorm, elevationNorm)`
  formula) - a completely separate scale from `VegetationField`'s
  calibrated range, sourced from Magic Sand's stock
  `bin/data/colorMaps/HeightColorMap.xml` (declared range -220mm to
  +220mm, inherited unmodified, with a known min/max inversion already
  flagged in `SandSurfaceRenderer.h`). A real elevation change can wash
  a color toward white via this path well before it's anywhere near
  triggering actual snow - the debug-snow toggle is the reliable way to
  tell the two apart.

## Accessible color palette pass (2026-09-24)

User asked to redesign the color palette for accessibility (colorblind-
safe) and visual appeal - a deliberate departure from ELF's literal
primary colors (and from this fork's own first-pass choices, which just
copied them), not a fidelity bug fix. Full color inventory done first
(via subagent) across shaders, `Critter`/`HumanAgent`, and legacy code -
confirmed 13 live color decisions across 3 groups (ecosystem terrain,
agents, operator status/calibration UI) plus a chunk of confirmed-dead
legacy `ofxDatGui` color code (colormap editor, `SaveModal`, commented-
out status panels) not worth touching. **Scoped to ecosystem terrain +
agents only**, per explicit user choice - the operator status/
calibration red-green-4x issue is real but out of scope for now.

**Two real problems found in the original scheme, not just "could look
nicer":**
- Fruit's peak color and `HUNTER_COLOR` were the *exact* same value,
  `(255,0,0)` - confirmed as the direct, literal cause of a real user
  question this session ("is all this red hunting humans?").
- Fruit's peak `(255,0,0)` vs shrub's peak `(0,255,0)` is the textbook
  worst-case pair for red-green colorblindness (the most common form) -
  pure complementary red/green with no lightness or blue-channel
  separation to fall back on.

**Design approach, per explicit user decision:** keep each species'/
role's original hue FAMILY (shrub green, fruit red/warm, nut teal/cool,
deer cyan, hunter red/warm, gatherer yellow, fisher blue) rather than a
free redesign - preserves ELF's own color intent and existing at-a-
glance intuition. Two different source palettes, each used for what
it's actually suited to:

1. **Species/agent colors** (shrub/fruit/nut + deer/hunter/gatherer/
   fisher) - hues from the **Okabe-Ito colorblind-safe categorical
   palette** (the standard reference for discrete/unordered categories,
   validated under deuteranopia/protanopia simulation): shrub→bluish-
   green `(0,158,115)`, fruit→vermillion `(213,94,0)`, nut→teal
   `(0,153,153)`, deer→a blue-shifted cyan `(0,180,216)` (distinct from
   nut's more green-leaning teal despite same cool family), hunter→
   crimson `(196,30,90)` (NOT vermillion - would've collided with
   fruit), gatherer→Okabe-Ito yellow `(240,228,66)`, fisher→deep indigo
   `(36,36,140)` (distinct from water's blue despite same family). Dead
   states (both species) left as black/dark-gray - neutral, no
   accessibility concern, not part of this pass.
2. **Background/elevation color** (water, and now negative-space/tie
   land too) - a **7-stop diverging ramp the user sourced themselves**
   (`#00429d,#5c83a6,#a5c2bd,#ffffe0,#ffa59e,#dd4c65,#93003a`, also
   confirmed colorblind-safe), applied via a new `terrainRamp()` GLSL
   function (piecewise-linear through all 7 stops) fed by
   `elevationNorm`. A sequential/diverging palette fits elevation
   specifically because elevation is physically continuous - unlike
   species identity, adjacent cells have similar height.

**Explicit design question raised and resolved: does a continuous
elevation ramp kill the "pixelated CA" look?** User asked directly
whether switching to option 2 (fully replacing species hue with the
elevation ramp) would still produce the pixelated cellular-automaton
look they specifically like. Answer, reasoned from mechanism rather than
guessed: **yes, it would be lost** if applied to actively-growing
vegetation - that speckled look comes from (a) independent per-cell
stochastic growth (no neighbor coupling in ELF's own mechanic, aside
from this fork's own spread layer) and (b) no-fade winner-take-all
coloring, both specific to the *vegetation* layer. Elevation itself has
neither property (adjacent cells are physically similar in height), so
a pure elevation ramp would look like a smooth gradient, and faking
per-cell noise on top would be decorative rather than representing real
simulation state - contrary to how every other visual in this system
has been built. **Resolution, per explicit user choice: hybrid.** The
diverging ramp is used ONLY for water and tie/negative-space land (both
already purely elevation-driven, no CA state to lose) - shrub/fruit/nut
keep the winner-take-all species-color mechanic entirely unchanged,
still full saturation the instant a cell establishes, still no fade.
Practical side effect: tie/negative-space land no longer renders flat
"no augmentation" white - it now shows the ramp's elevation-appropriate
tone, and vegetation still visibly claims a cell against that (arguably
more visible than against flat white before, not less).

**Nut's color formula changed at the root, not just gated.** Previously
`(0,h,h)` (black at low elevation, the direct cause of the "random black
speckle" bug fixed just above) - now `mix(peakTeal, white, elevationNorm)`,
the SAME peak-to-white pattern shrub/fruit already used. Nut can no
longer render black at any elevation. `NUT_VISIBILITY_THRESHOLD` is kept
as a secondary guard against a single lucky tick flashing to full
saturation instantly, not as the primary fix anymore - the formula
itself is now the primary fix.

**Left untouched, out of scope for this pass:** snow (flat white/debug
pale blue - never part of the accessibility problem), contour lines
(flat black - already understood, not confused with vegetation), all
dead-agent-state colors (neutral grays/black), and the operator status/
calibration UI's red-green-4x pattern (the single biggest colorblind
risk found in the full inventory, deliberately deferred - user's
explicit scope choice was ecosystem terrain + agents only).

Implementation: `heightMapShader.frag` (both `shadersGL2` and
`shadersGL3` variants, kept in sync per that file's own convention) -
new `terrainRamp()` function, water/tie branches call it, shrub/fruit/
nut branches use new `mix(peak, white, h)` formulas. `Critter.cpp`
(`BODY_COLOR`) and `HumanAgent.cpp` (`FISHER_COLOR`/`HUNTER_COLOR`/
`GATHERER_COLOR`) updated to match. All still fully live-tunable where
a GUI control already existed (deer's `ImGui::ColorEdit3`); the rest
remain compile-time constants, same as before this pass.

**Follow-up, same day: the first-pass species/agent colors above were
too conservative - confirmed by the numbers, not just a subjective
impression.** User pushed back ("the new colours are still quite close
to the old ones"). Checked RGB distance from each original value before
changing anything further: gatherer `(255,255,0)→(240,228,66)` moved
only ~17% of the maximum possible RGB distance, fruit `(255,0,0)→
(213,94,0)` ~23%, deer `(0,255,255)→(0,180,216)` ~19% - genuinely small
moves, not a false impression. Root cause: Okabe-Ito is tuned to fix
ONLY the specific red-green collision that was the actual accessibility
bug (fruit/hunter identical, fruit/shrub the classic red-green
worst-case pair) - it deliberately doesn't move colors that were never
part of that problem (yellow, cyan, blue) far from their familiar
identity, which serves data-viz legibility but not "look visually bold/
distinct," which was also part of the original ask.

Replaced with bolder, more saturated values chosen by the same
principle that actually makes Okabe-Ito colorblind-safe - separation
via the RED and BLUE channels and overall lightness, not hue alone
(deuteranopia drops the green-sensing cone, so two colors differing
only in "how green" still merge) - while matching the jewel-toned
saturation of the diverging ramp the user sourced themselves, for one
cohesive palette rather than a muted categorical set paired with a bold
sequential one:
- Shrub: rich emerald `(0,168,89)`, R=0.
- Fruit: bold orange-red `(230,90,20)`, high R separates it from shrub;
  real G/B keeps it off pure red.
- Nut: saturated teal `(0,172,172)`, R=0/high B.
- Deer: bright "electric" cyan `(0,190,255)`, distinctly bluer/brighter
  than nut despite the same cool family.
- Hunter: bold crimson `(200,20,90)` - real B component separates it
  from fruit via blue, not just hue.
- Gatherer: rich gold/amber `(255,190,0)` - full R, strong G, zero B -
  clearly warmer/more saturated than flat lemon yellow.
- Fisher: deep indigo `(30,30,130)` - notably darker than deer and
  water despite the same blue family.

Still untested on real hardware for how these actually project on
sand - the same open question as the diverging ramp itself.

## Discovered mid-session (2026-09-24): local hardware checkout was 6 commits
behind, and likely explains earlier misdiagnoses

User realized partway through the palette feedback round that they
hadn't been running `git pull` on the machine actually used for
hardware testing (Visual Studio, F5 build/run - never touches git on
its own). Checked precisely rather than guessing: their local HEAD was
`82dec04` ("Log that the FPS fix only got to 5fps..."), six commits
behind this session's `54f63d0` at the time. Missing: the ELF-literal
temperature revert (`f261c2b`), the 0.9 temperature raise (`289b6e8`),
the nut visibility threshold/black-speckle fix (`a7bb03d`), and BOTH
color palette passes (`e7ae619`, `54f63d0`) - `d59dcde` is CLAUDE.md-only,
no code.

**Real accounting of what that means for the intervening conversation:**
- The Debug->Release build-config fix is unaffected (a local Visual
  Studio setting, not tracked by git) - genuinely real.
- The nut-black-speckle diagnosis was still correct, because at `82dec04`
  nut's shader formula was still the original unfixed `(0,h,h)` - the
  explanation matched what was actually running, even though the fix
  itself hadn't been tested yet.
- The "-135.6mm water line / no water showing / ELF reference figure"
  investigation was conducted against the ORIGINAL `0.75`/`0.5`
  temperature defaults, not ELF's literal `0.784`/`0.784` (which is what
  the "water is mathematically impossible" diagnosis was actually
  about) - `0.75`/`0.5` structurally has a reachable water band by
  design, so whatever actually caused that round's black/no-water look
  has a different, still-unconfirmed cause (most likely the elevation
  range bounds drifting out of calibration again, the same class of bug
  fixed once already this project via "Reset range to ±calibrated
  ceiling") - reopened, not resolved, pending a real test on current code.
- "The new colours are still quite close to the old ones" was accurate
  but uninformative about the Okabe-Ito palette specifically - at
  `82dec04` neither palette commit existed, so the colors being judged
  were the completely unmodified ELF originals, not a real comparison.
  The subsequent "push it bolder" revision (see above) is still
  reasonable on its own merits, but wasn't actually validated against
  the Okabe-Ito intermediate on hardware - that step got skipped
  entirely, not rejected.

**Separately found while diagnosing this: an uncommitted local edit that
likely explains the ORIGINAL "black patches"/"is this negative space"
investigation better than the nut-formula bug did.** `git diff` on the
local checkout showed `heightMapShader.frag`'s pre-classification
default color changed from the repo's `vec4(1,1,1,1)` (white) to
`vec4(0,0,0,1)` (black), consistently in both GL2 and GL3 variants -
present the whole time, never committed. Since (at `82dec04`) ties fell
straight through to this default with no override, a black default
meant nearly the entire unvegetated field rendered black, with only
actual grown vegetation showing as colored specks - a much better match
for "black background with scattered colored speckle" than isolated
nut pixels would be. **User's explicit decision: keep the black
default.** Committed as the new tracked value (was never going to
survive a `git reset`/fresh clone otherwise) with updated comments
explaining the projector-physics rationale (can't project true black -
this reads as "add no light, let the sand show" rather than "cast a
neutral white wash"). **Important caveat flagged to the user:** this
default now only shows through briefly before `hasVegetation` first
becomes true, or with vegetation disabled - the hybrid palette pass
above already made ordinary tie/negative-space land render via
`terrainRamp()` instead, regardless of this value, so black-vs-white
here is now a much smaller-impact, near-cosmetic startup-flash choice
than it would have been in the code the user was actually testing
against when they first made this local edit.

**Action item, not yet done:** user needs to `git pull
origin ss/ecosim-model-visuals-kiqw12` (checked first that the three
locally-modified settings XMLs - `calibration.xml`/
`kinectProjectorSettings.xml`/`sandSurfaceRendererSettings.xml`, real
auto-saved calibration data from running the app - aren't touched by
any pending commit, so they're not a merge risk) and do a full Release
rebuild before trusting ANY visual observation going forward. Until
that happens, water/snow reachability in particular should be treated
as re-opened, not resolved.

**UPDATE (2026-09-25) - pull completed, water/snow reachability re-
investigated on current code and genuinely resolved this time, with
real measurements instead of guesses.** First check on current code:
elevation range bounds A/B were STILL at an arbitrary `-300.000`/
`300.000` mm - the exact same recurring bug as before (Magic Sand's own
measured ceiling reads 142.6mm) - so the very first real observation
after finally syncing reproduced a known, already-understood issue.
Fixed the same way as before: "Reset range to ±calibrated ceiling"
button, snapping to `-142.606`/`142.606`.

That alone wasn't enough - even with a correctly calibrated range,
`TEMPERATURE=0.9`/`LIVING_RANGE_FRACTION` at ELF's literal `~0.784`
(from the 2026-09-24 revert) put the water line at `-104.5mm`. Rather
than guess whether that's reachable, asked the user to directly measure
their box's real limits via the Vegetation panel's raw-elevation
readout: **max hand-dig = -41.8mm, max hand-built mound = +80mm**.
-41.8mm doesn't even reach the best case achievable via TEMPERATURE
alone (TEMPERATURE=1.0, sacrificing all snow headroom, only gets water
to -81mm) - confirming with direct measurement, not just the earlier
general finding, that ELF's literal `LIVING_RANGE_FRACTION` is
impractical on this specific box and needed to move, not just
`BASE_TEMPERATURE`.

**Both values now solved algebraically from the two real measurements**
rather than guessed the way the original `0.5` departure and the `0.9`
first-guess both were: target water line -35mm (7mm margin under the
dig limit), target snow line +70mm (10mm margin under the mound limit).
On this box's calibrated range, `snowLevelFrac = TEMPERATURE` solves
directly to **0.745**, and `waterLevelFrac = TEMPERATURE -
LIVING_RANGE_FRACTION` solves to **LIVING_RANGE_FRACTION = 0.368**.
User explicitly confirmed reverting `LIVING_RANGE_FRACTION` away from
ELF's literal value a second time, now with direct measured
justification rather than a general prior finding. Both constants
updated in `VegetationField.cpp`/`.h` with the full derivation in
comments, so if this box's calibration or physical sand depth changes
later, the same method (measure dig/mound limits via the panel, solve
for both fractions from the two targets) can be redone rather than
hand-tuning sliders by feel again.

**Template for redoing this on a different installation, or if this
box's setup changes:** (1) click "Reset range to ±calibrated ceiling"
first, always - rules out the recurring miscalibration bug before
touching anything else. (2) Measure real dig/mound limits via the raw-
elevation readout, subtract a few mm margin from each. (3) Convert both
mm targets to fractions of the calibrated range. (4) `TEMPERATURE =
BASE_TEMPERATURE = ` the snow target's fraction. (5)
`LIVING_RANGE_FRACTION = TEMPERATURE - ` the water target's fraction.

**FIXED (2026-09-25) - water wasn't reading as distinctly blue enough
to tell apart from negative space or shrub, once actually reachable.**
Root cause: water shared `terrainRamp()` with tie/negative-space land,
sampled at the raw global `elevationNorm`. Water's whole reachable
depth only spans `0..waterLevelFrac` (a fraction of the full ramp,
`~0.377` with the current measured values) - so water's SHALLOW edge
sampled the ramp at essentially the same `t` as the adjacent tie land
just above the water line, meaning the two rendered nearly identically
right at the shoreline instead of showing a clear edge.

Fix, per explicit user request: water now uses its own dedicated
2-stop gradient (`#00429d`/`#4b73a2` - the user's first two ramp
stops), via a new `waterRamp()` GLSL function, with `elevationNorm`
renormalized to water's OWN reachable range
(`elevationNorm / waterLevelFrac`) rather than the raw global one - so
the full deep-blue-to-medium-blue span is always used regardless of how
narrow the actual reachable water band is. Deepest water is always the
darkest stop, shallowest reachable water is always the lighter one,
neither ever bleeds into the paler stops tie land uses. Requires
`waterLevelFrac` (`TEMPERATURE - LIVING_RANGE_FRACTION`) as a new
uniform, computed in `SandSurfaceRenderer::drawSandbox()` and passed to
both `heightMapShader.frag` variants (GL2/GL3, kept in sync). Tie/
negative-space land is unaffected - still the full 8-stop `terrainRamp()`
at the global `elevationNorm`.

**SECOND BUG in the same fix, found when actually tested on hardware
(2026-09-25): water rendered as one flat, uniform color - no gradient
visible at all.** The renormalization above (`elevationNorm /
waterLevelFrac`) silently assumed water's elevation can reach all the
way down to `elevationNorm=0` - the calibrated FLOOR (`elevationMin`).
But `elevationMin` is never actually measured (`setElevationRange()`'s
own header note: it just mirrors the calibrated CEILING symmetrically)
- on this box it sits at `-142.606mm`, while the user's real measured
max dig is only `-41.8mm`. Water's actual reachable `elevationNorm`
therefore only ever spans `~0.353-0.377` - a tiny sliver near the TOP
of the theoretical `0..waterLevelFrac=0.377` range the fix assumed -
so `waterDepthT` stayed pinned near `1.0` (the lightest shade) no
matter how deep the user actually dug.

Fix: added `WATER_GRADIENT_FLOOR_FRAC` (default `0.353`, live-tunable)
- the user's own measured `-41.8mm` dig limit expressed as a fraction
of the current calibrated range, the same conversion already used for
`waterLevelFrac` itself. Deliberately a SEPARATE, cosmetic-only
constant, not a change to `elevationMin`/`elevationMax` themselves -
changing those would have shifted the total calibrated range and
invalidated the already-correct `TEMPERATURE=0.745`/
`LIVING_RANGE_FRACTION=0.368` derivation (classification was already
right; only the gradient's cosmetic renormalization was broken).
`waterDepthT` now interpolates between `WATER_GRADIENT_FLOOR_FRAC`
(darkest) and `waterLevelFrac` (lightest) instead of between `0` and
`waterLevelFrac`. New `waterGradientFloorFrac` uniform, same wiring
pattern as `waterLevelFrac`. Same caveat as the other measured
constants: if this box's calibration or physical sand depth changes,
re-measure the real dig limit and re-derive the fraction the same way.

**RETUNED again (2026-09-25), same day, once actually tested on
hardware:** `0.353` (the exact measured dig limit) turned out too
strict. The water line sits at `~-35.1mm` and the absolute dig limit is
`-41.8mm`, so the FULL gradient only had `~6.8mm` of real depth to work
with - reaching full dark required near-maximal digging every time,
with almost no margin. User's exact words: "I really have to dig for
it... seeing a lot of the lighter shade, and very little of the darker
shade." Retargeted to full dark at just **3mm past the water line**
instead of the absolute dig limit - comfortably, reliably reachable,
leaving `~3.8mm` of margin below that where water simply stays fully
dark rather than needing to be re-found on every dig.
`WATER_GRADIENT_FLOOR_FRAC` is now `0.366` (`waterLevelMM - 3mm`
expressed as a fraction of the current calibrated range). General
lesson for any future retuning of this value: target a few mm past the
water line, not the absolute measured dig limit - the dig limit answers
"is any gradient reachable at all," not "where should full darkness
sit."

**RETUNED A THIRD TIME (2026-09-28) - the real bug wasn't the target
depth, it was that the floor was a FIXED absolute value while the water
line itself moves.** User, days later: "the darker blue doesn't show up
until I've hit the bottom of the sandbox" - despite the "3mm past the
water line" fix above. Root cause: `WATER_GRADIENT_FLOOR_FRAC=0.366`
was an absolute elevation fraction (~-38mm on the calibrated range at
the time it was derived), computed once and hardcoded. But
`waterLevelFrac` (`TEMPERATURE - LIVING_RANGE_FRACTION`) is not static -
`TEMPERATURE` eases upward automatically with sculpting activity
(`ACTIVITY_TO_TEMPERATURE`/`MAX_TEMPERATURE_OFFSET`), which raises the
water line (shallower, floods more land - same mechanic documented
above). Every time that happened over the following days of normal use,
the gap between the fixed floor and the now-shallower water line
widened, so reaching full dark required digging further and further -
exactly matching the symptom.

**Fix: `WATER_GRADIENT_FLOOR_FRAC` is now a SPAN (how far below
whatever the water line CURRENTLY is), not an absolute position.**
Value unchanged in magnitude (`0.0105`, still ~3mm worth on this box's
range - the tracking bug was the confirmed problem, not the span size).
`SandSurfaceRenderer::drawSandbox()` now computes the actual floor
fresh every frame as `waterLevelFrac - WATER_GRADIENT_FLOOR_FRAC`
before passing it to the shader, so it automatically follows the water
line wherever `TEMPERATURE` currently puts it, instead of drifting out
of reach. GUI slider relabeled "Water gradient span" and range tightened
to `0..0.2` (was `0..1`, sized for the old absolute-position framing).
**Broader lesson, now confirmed twice on this exact feature:** any
value derived from "the current water/snow line" needs to be expressed
*relative to* `waterLevelFrac`/`snowLevelFrac` and recomputed live, not
snapshotted as an absolute elevation - those two fractions are live,
moving quantities by design (that's what makes Temperature "participant-
driven" at all), not one-time calibration constants like
`elevationMin`/`elevationMax`.

## Species elevation gradient (shrub/fruit/nut) barely visible (2026-09-28)

Same root-cause CLASS as the water-gradient bugs above, found on the
same visit while explaining the fruit peak-to-white gradient's design:
user reported it's "just barely noticeable" on a real mound. Checked
the actual reachable numbers before assuming it's a hardware/projector
limit (per this file's own standing rule) rather than retuning by feel.

`mix(peakColor, white, elevationNorm)` (shrub/fruit/nut in
`heightMapShader.frag`) uses the RAW global `elevationNorm` (0..1 across
the whole calibrated sensor range) as its mix parameter. But shrub/
fruit/nut can only ever classify as themselves between the water line
and the snow line - `VegetationField`'s own band tests exclude
everywhere else. On this box's tuned values (`waterLevelFrac~0.377`,
`snowLevelFrac` = `TEMPERATURE~0.745`), that means land vegetation NEVER
actually sees `elevationNorm` outside `[0.377, 0.745]` - a ~0.368-wide
slice sitting well above 0, not the full `0..1` domain the mix assumed.

Computed fruit's mix at both real endpoints to confirm quantitatively:
at `elevationNorm=0.377` (right at the water line, the lowest land ever
gets), fruit renders `(0.94, 0.60, 0.43)` - already mostly washed to
white. At `elevationNorm=0.745` (right at the snow line, the highest
land ever gets before becoming snow), it's `(0.98, 0.84, 0.77)` - a
near-white peach. The fully-saturated peak `(0.90, 0.35, 0.08)` -
what the "orange" in "soft orange gradient" is actually supposed to
look like - is mathematically unreachable by any real land cell, since
that only happens at `elevationNorm=0`, the calibrated floor, which
classifies as water long before a cell gets anywhere near it. The RGB
distance between the two real endpoints is ~37% of the full peak-to-
white distance, and that visible 37% is entirely within the
already-pale half - i.e., "barely noticeable" is the correct, expected
result of the code as it stood, not a perception issue or a hardware
limitation.

**Fix: renormalize shrub/fruit/nut's mix parameter to the actual
reachable land band**, the same technique already used for water
(`waterRamp`) and already proven on hardware. New `landT`:
```glsl
float landT = clamp((elevationNorm - waterLevelFrac) / max(snowLevelFrac - waterLevelFrac, 0.0001), 0.0, 1.0);
```
computed once in `main()` right after `elevationNorm`, and used in place
of `elevationNorm` in all three species' `mix()` calls. Requires a new
`snowLevelFrac` uniform (`= TEMPERATURE`, ELF's own snow threshold - see
`VegetationField.h`), set alongside `waterLevelFrac` in
`SandSurfaceRenderer::drawSandbox()`. `terrainRamp()` (tie/negative-space
land) is deliberately UNCHANGED - it's meant to represent the whole
elevation spectrum including water and above-snow-line high ground, and
is already confirmed correct on hardware; this renormalization only
applies to the three species' own peak-to-white modulation. Both shader
variants (GL2/GL3) updated in sync, per this file's usual convention.

Not yet tested on real hardware. Same live-tracking property as the
water-gradient fix applies here too, automatically: since `landT` is
computed from `waterLevelFrac`/`snowLevelFrac` fresh every frame rather
than any snapshotted constant, it stays correct as `TEMPERATURE` eases
with sculpting activity, with no separate fix needed later.

## Hydrology phase, first slice: depression-filling pools (2026-09-28)

Started the third piece of the succession-model phase (the other two -
neighbor-spread growth and real seed placement - are separately tracked;
spread is done, seeding still isn't started). Before designing anything,
both source papers referenced in the "Future phase" section above were
actually read in full this session (they'd only ever been summarized
secondhand before) - `pdftoppm`/poppler-utils had to be installed first,
neither was renderable without it.

**Correction to this file's own earlier description: `ARsandbox014.pdf`
is NOT the Grasshopper/vector-field paper this file previously said it
was.** That description was simply wrong (most likely a mixup from an
earlier session that never actually opened the file). Read in full, it's
Murgatroyd/Butler/Gaffney's own paper *about the ELF AR sandbox itself* -
the exact system this whole project ports. It's a nice independent
confirmation that the port's mechanics (temperature-driven water/snow
lines, three plant types, deer/human behavior and colors) match ELF's own
authors' description of their design intent, but it has zero hydrology
content. It separately describes a second, unrelated simulation ("ELF
Activity Traces" - biomes, settlements, and archaeological survey proxies
like pollen/magnetometer data) that has nothing to do with water flow
either - not relevant to this phase.

**Liu's "The Third Simulation" (the actually-correct fluvial paper) turned
out to contain no portable algorithm either**, just for a different
reason: it's a landscape-architecture exhibition paper, not a technical
one. It states the water simulation was "developed by the author based on
[a] 2D Lattice Boltzmann model in Processing," visualized in Grasshopper
as flow-direction/velocity/vorticity vector fields, plus a separate
agent-based seed-dispersal system (tagged-object planting, propagation
driven by the hydraulic sim + each species' own properties/competition) -
but contains no equations, pseudocode, or implementation detail at all.
There is nothing in either paper to literally port.

**User's explicit decision given this, made via two AskUserQuestion
rounds before any code was written:** provide the papers rather than
proceed on a secondhand description (done, see above), and target
"lightweight pooling" as the first slice's fidelity level specifically
because the CA spread layer's earlier 60fps->1-2fps regression on this
exact hardware (see above) made a naive real-time cellular flow
simulation too risky to attempt first.

**Design, since no source algorithm existed to be unfaithful to:** this is
an original addition, in the same posture as the CA spread layer -
`BDlocation.getWater()` is itself a flat per-cell threshold with zero
connectivity/pooling concept, confirmed from source during the original
port, so pooling is a deliberate ELF departure, not a fidelity gap being
closed. Implemented as a standard priority-flood depression fill (Barnes
et al. - a well-established DEM/watershed-analysis technique, not
invented from scratch): seeded from every cell already below the flat
`waterLevelFrac` line (this grid's only guaranteed "sea," since there's no
real map edge to seed from the way a real-world DEM would), flooding
outward and capping at the snow line, so an enclosed low area whose floor
sits ABOVE the flat water line but is fully rimmed below the snow line now
fills with its own local pool up to its lowest pour point - a sculpted
bowl no longer has to reach all the way down to the single global sea
level to hold water.

Layered on top of, not replacing, the existing flat check: `poolLevel`
(new per-cell `cv::Mat`, same units as `waterLevelFrac`) defaults to the
flat line everywhere and only rises within a real enclosed basin, so
`isWater = elevFrac < poolLevel[cell]` collapses to the exact old
behavior for any cell not part of a basin. `isWaterAt()` (used by
Critter/HumanAgent movement-blocking and fishing eligibility) gets the
same check, so agents correctly treat a filled pool as real water too, not
just what's rendered.

**Performance, checked by the numbers before assuming "periodic, not
per-frame" was enough** (the CA spread layer's original mistake was
assuming a whole-grid-every-frame cost was free without checking first):
~519x295 grid = ~153K cells, O(n log n) binary-heap flood, estimated
single-digit milliseconds in a Release build. Recomputed on a plain timer
(`POOL_RECOMPUTE_INTERVAL_SEC`, default 1.0s, tunable) rather than every
frame - real but small and bounded, unlike the previous regression's
unbounded per-frame-per-cell-per-species cost. A cell already inside an
established pool still reacts to digging/mounding instantly every frame
(it re-reads live elevation against the last-computed pool geometry);
only whether a brand-new basin exists at all lags by up to the interval.
`recomputePools()` reuses `previousElevation` (already fresh every frame
for the existing activity/temperature computation) rather than sampling
the Kinect a second time. Same `ENABLE_SPREAD`-style A/B toggle
(`ENABLE_POOLING`, default on) for instantly ruling this out as a
regression cause without a rebuild, plus a GUI readout of last-recompute
cost and pooled-cell count and a manual "recompute now" button - all new
in `VegetationField.h`/`.cpp`'s Hydrology section.

**No shader or `SandSurfaceRenderer.cpp` changes needed** - a pooled cell
sets the exact same alpha=255 the texture upload already writes for flat
water, so it renders through the existing `waterRamp()` path with zero
new plumbing.

**Known, explicitly-flagged first-slice limitation:** the water shader's
depth gradient (`waterRamp()`/`waterDepthT`) is still anchored to the
single flat `waterLevelFrac`/`waterGradientFloorFrac` uniforms. Every
perched-pool cell sits above `waterLevelFrac` by construction, so
`waterDepthT` clamps to 1.0 for all of them - a perched pool always
renders in the lighter "shallow" shade, never the darker deep-water tone,
regardless of its own real depth within its basin. Not broken (still
reads clearly as water), just uniformly "shallow-looking." A real fix
needs a second per-cell texture channel carrying each pool's own local
floor/ceiling to the shader - genuine scope, deliberately deferred as the
natural second slice rather than attempted here.

Not yet tested on real hardware - `POOL_RECOMPUTE_INTERVAL_SEC`'s default
and the cost estimate above are both first-guess/estimated, same
uncertainty flag as `SPREAD_RADIUS_MM`/`SPREAD_CHANCE_MULTIPLIER` got when
the spread layer first shipped.

**Still not done:** real hand-placed seeding (the third piece of this
phase) - pooling only adds where water CAN sit, not where a participant
plants something. Also still open: giving perched pools their own depth
gradient (see the limitation above), and revisiting whether a real-time
flow/velocity layer is worth the regression risk once this simpler slice
is confirmed working on hardware.

**FOUND AND FIXED same day, confirmed broken on real hardware almost
immediately: this was a real design mistake, not a hardware/tuning
issue.** User reported "blinking, and flat areas are filling up with
water," with a screenshot showing blue speckle scattered widely across
ordinary terrain, not small contained ponds. Root cause: the flood above
only stopped at the snow line - but "land between the water line and the
snow line" is most of the terrain by definition, so instead of filling
small enclosed bowls it was computing a full watershed pour-point flood
across nearly the entire connected landmass. A priority-flood correctly
computes, for every reachable cell, the elevation water would need to
reach via the CHEAPEST path from the sea - which is the right tool for
"what would this DEM look like under infinite rainfall," but the wrong
one for "highlight small ponds a person just sculpted," because on any
normal, not-fully-partitioned terrain nearly everything eventually
connects to the sea via SOME saddle point below the snow line. The
"blinking" is the same bug from a different angle: with the flood
essentially unbounded, a couple mm of ordinary sensor noise at some
distant saddle point could flip whether huge swaths of the board were
reachable, once per ~1-second recompute cycle.

**Fix: `MAX_POOL_RISE_FRAC`** (default `0.05`, ~15mm-equivalent on this
box's calibrated range, live-tunable) - caps how far ABOVE the flat water
line a pool is allowed to rise. The flood simply stops relaxing a
neighbor once filling it would exceed this, bounding pools to genuinely
small, locally-enclosed dips near the water line instead of the whole
reachable landmass. Same fix incidentally also addresses the blinking:
bounding the flood's reach means a noise-driven flip at some saddle point
now only affects a small, local pond rather than potentially the whole
board. GUI slider added alongside the existing pooling controls
explaining the fix, in case pools still look too broad/unstable at the
first-guess default and need tightening further.

**FOUND same visit, a DIFFERENT bug from the one above (that one was huge
swaths of the board flipping once a second; this one is the water's edge
itself flickering fast, every frame): "it's flickering around the
waterline."** Root cause: `isWater` was always a raw instantaneous
comparison against live elevation, with no hysteresis at all - a cell
sitting within Kinect's own known ~1-2mm depth noise floor (the same
physical noise `ACTIVITY_NOISE_FLOOR` already exists to filter, in the
unrelated context of the temperature/activity computation) of whatever
threshold applies flickers in and out of "water" every single frame from
sensor jitter alone. This would have been true of the ORIGINAL flat-
threshold check too, before pooling ever existed - just apparently never
reported, most likely because a thin, fast-flickering boundary line is
far less visually jarring than "half the board flooding," so it went
unnoticed until attention was specifically on the shoreline (prompted by
fixing the bigger bug above).

**Fix: standard Schmitt-trigger hysteresis.** New `waterState` (persistent
per-cell `cv::Mat`, "is this currently water") plus `WATER_HYSTERESIS_FRAC`
(default `0.014`, ~4mm-equivalent, a bit past `ACTIVITY_NOISE_FLOOR`'s
~3mm so it reliably beats the same noise, not just usually) - a cell must
cross clearly PAST the threshold by this margin to change state at all;
otherwise it keeps whatever state it already had. Applied in `update()`'s
per-cell loop (feeding both the density-zeroing and the alpha-channel
texture write) and reused as-is in `isWaterAt()` (movement-blocking/
fishing) rather than that function doing its own separate raw check -
previously `isWaterAt()` and `update()` computed water status
independently, which given this bug could in principle have disagreed
with each other on any given frame; reading the one shared, already-
stabilized `waterState` fixes both the flicker and that latent
inconsistency at once. `recomputePools()`'s own seeding is UNCHANGED
(still a raw check against the flat line) - deliberately scoped out of
this fix since the reported symptom reads as a fast, continuous flicker
(consistent with the per-frame path above), not a once-a-second pop
(which would implicate the recompute cycle's seeding instead); revisit
if flicker is still visible specifically at that ~1s cadence after this.

Not yet re-tested on real hardware after either fix.

## Threshold-ring legibility pass tried and reverted (2026-09-28)

The live water/snow-line threshold rings described above (violet/lilac
outlines tracing the current water and snow lines, reusing the contour-
line pipeline) were implemented and pushed, then tested on real hardware
the same day. User's verdict, with a screenshot: **"it looks terrible.
I want to go back to the way it was before."** Reverted cleanly via
`git revert` (commit `5e83973`, reverting `c865fd3`) rather than editing
around it - a clean revert of the whole feature, not a partial walk-back,
since the feedback was a flat rejection of the visual result, not a
specific tunable (color, thickness, etc.) to adjust.

**Not investigated further before reverting** - the user's own screenshot
showed jagged, sketchy-looking violet/lilac outlines that read as messy
line noise rather than clean rings, but no attempt was made to diagnose
why (line rendering shape/anti-aliasing? the specific color choice? the
concept of highlighted rings itself, regardless of execution?) before
reverting, per the user's explicit "back to the way it was" instruction -
that's a revert-first, ask-later situation, not one to negotiate a
smaller fix in the moment.

**Open question for if/when legibility work resumes:** was the problem
the *specific implementation* (jagged line quality, color choice) or the
*concept* (any highlighted ring at all, regardless of how clean)? Worth
asking directly before attempting a second pass at the same underlying
goal (making the water/snow lines visible in the sandbox itself) - that
goal and the legibility concern behind it are unchanged by this revert,
only this particular execution of it.

User answered this directly the same session: **"I think it was the
concept itself."** Not pursued further as a standalone fix - superseded
by the much larger direction-setting conversation below, which answers
the same underlying legibility problem a different way.

## Strategic reset: physical legibility vs. disclosure legibility (2026-09/10)

Triggered by the user's own doubt, raised unprompted right after the
threshold-ring revert: "I'm not quite sure what doesn't feel right about
the project as is... 'where do we go from here?', 'I can barely
understand the interaction mechanic, so how will others?' 'this isn't an
ecological succession model yet'." Not a request to fix a specific bug -
a real question about whether the project's current trajectory (ELF port
-> CA spread -> pooling -> threshold rings) was actually heading toward
"ecological succession model," the project's own stated main goal (see
the "Future phase" section above), or just accumulating disconnected
features.

Honest assessment given directly: the project genuinely does not have
real succession dynamics yet - growth is still per-cell-independent
(spread layer aside), there's no real competitive displacement between
species over time, and the water/snow mechanic is a classification, not
a process. That gap is real, not a misunderstanding on the user's part.

**Grounded in two concrete reference points the user provided, not
abstract discussion:**

1. **The user's actual SSHRC (Social Sciences and Humanities Research
   Council of Canada) doctoral grant proposal** (`SSHRC_Application_v3.pdf`),
   shared specifically to check this codebase against real stated
   research goals rather than drift from them. The proposal frames
   Shifting Sands as already-complete infrastructure feeding INTO a
   PhD-level research question about data provenance/trust in tangible
   models - a "disclosure layer" toggling between data-free/data-informed/
   data-driven representations. That framing implies a different job for
   the CURRENT (Master's-level) phase than "add more simulation features
   indefinitely."
2. **SND (Sand Noise Device)** by Jay Vandyke (jayvandyke.com/snd) - an AR
   sandbox art installation the user introduced as a concrete aesthetic/
   legibility bar: virtual objects with real physics (mass, gravity,
   momentum) against the sand's shape - rolling downhill, slowing uphill,
   pooling in pits - legible with zero explanation needed, because the
   behavior itself is the intuitive, embodied metaphor. Confirmed via a
   user-provided screenshot of the site's own description text (direct
   web access to jayvandyke.com/makezine.com was blocked at this
   environment's outbound proxy - confirmed via direct curl returning 403
   on both domains, a general restriction, not domain-specific - so the
   user pasted the relevant text directly rather than Claude fetching it).

**Resolution, explicitly confirmed by the user:** two genuinely different
kinds of "legibility" were being conflated. SND's is *physical/embodied*
legibility - understanding through intuitive interaction with no added
UI. The SSHRC proposal's is *epistemic/disclosure* legibility - understanding
where the DATA came from and how much to trust it, which needs an actual
informational UI layer. **"The master's thesis should focus on the
physical legibility for the core terrain mechanic itself, leaving open
the possibility to pursue the disclosure UI for the data provenance
question in the PhD."** This is now the project's explicit phase
boundary: Master's = make the terrain mechanic itself (water, growth,
succession) physically intuitive, SND-style; PhD = the data-provenance
disclosure UI, deliberately not started now.

Given that split, three options for closing water's specific physical-
legibility gap (currently an invisible threshold, no sense of flow or
slope-response) were put to the user: (1) shader-only flow-direction
shimmer (cosmetic, cheapest), (2) non-simulated decorative flow particles
(cosmetic, moderate), (3) real flow/transport dynamics - water that
actually flows downhill and pools through genuine per-cell physics,
replacing the current classification entirely. **User's explicit choice:
"I want 3"** - the real version of what SND does, not an imitation of it,
consistent with the physical-legibility framing just agreed on.

**Not yet revisited, status unclear rather than abandoned:** a separately-
discussed succession mechanic (fruit=pioneer/nut=intermediate/shrub=climax
with rank-based competitive displacement over time) that the user agreed
tracked correctly ("yes, that tracks for me exactly") before this
strategic conversation started. It was superseded in immediate priority
by the water-physics decision, not rejected - worth asking the user
directly once flow dynamics is confirmed working on hardware, rather than
assuming it's still wanted or still deprioritized.

## Hydrology phase, second slice: real flow/transport dynamics - REPLACES pooling (2026-10)

Implements option 3 above. **This removes the "lightweight pooling"
depression-fill mechanic documented earlier in this file
(`ENABLE_POOLING`/`POOL_RECOMPUTE_INTERVAL_SEC`/`MAX_POOL_RISE_FRAC`/
`recomputePools()`/`poolLevel`, and the `WATER_GRADIENT_FLOOR_FRAC`
cosmetic gradient-proxy fix built on top of it) entirely, rather than
adding flow alongside it.** Pooling's own header note already called
itself "the cautious version of real flow, until real flow is safe to
attempt" - that condition is now met, and running both at once would
mean two different systems independently deciding "is this cell water,"
a correctness risk rather than redundant safety. Depression-filling (the
whole point of pooling) is subsumed as an emergent property of real flow
instead: water flowing into an enclosed basin has nowhere else to go and
settles at the basin's real rim through the same per-frame dynamics, no
separate priority-flood graph algorithm needed.

**Algorithm: virtual pipes (Mei/Decaudin/Hu-style), CPU-side, every
frame.** Each cell now carries a persistent water DEPTH (`waterDepth`/
`waterDepthNext`, ping-pong CV_32F buffers, same fraction-of-range units
as everything else in this class) on top of its terrain elevation; the
water SURFACE height is `terrain + depth`, and flow between 4-neighbors
is driven by surface-height differences, transported proportional to
that difference. Implemented as two passes per frame (`stepFlow()` in
`VegetationField.cpp`) specifically to be unconditionally stable without
a CFL-style timestep restriction: pass 1 computes each cell's own
outflow-limiting SCALE factor (the total it would send to all downhill
neighbors, capped so it never exceeds that cell's own current depth -
self-limiting, not a global bound on `FLOW_RATE`/`dt`); pass 2 applies
each cell's net change (outflow scaled by its OWN factor, inflow scaled
by the NEIGHBOR's factor, both recomputed from last frame's state) and
writes only `waterDepthNext` - a pure Jacobi update, no read-after-write
hazard the way an in-place sweep would have. The existing flat
`waterLevelFrac` (already tuned from real hardware measurements, see
above) is reused as-is as the sea's surface elevation: any cell below it
has its depth clamped up to at least `waterLevelFrac - elevFrac` AFTER
the flow step, every frame - a real boundary condition the simulation
continuously maintains, not a one-time seed, so flow can't drain the sea
itself.

**Cost budget, checked before writing anything** (the CA spread layer's
actual lesson, re-examined rather than over-applied): ~519x295 grid,
~153K cells, two full passes of 4-neighbor arithmetic each - additions/
multiplications/comparisons only, no `ofRandom`/`cos`/`sin`/`sqrt`. The
CA regression's real cause was specifically *expensive per-sample
operations at volume* (up to 3 species x 8 samples x every cell, each
with 2x `ofRandom` + trig + `sqrt`), not "touching every cell every
frame is inherently unsafe" - `recomputePools()` already ran a whole-grid
pass every ~1s safely, and this is a categorically cheaper operation
class still, run every frame instead. `ENABLE_FLOW` is still a real A/B
kill switch (default on) in case this estimate is wrong on real hardware.

**Rendering: the vegetation texture's alpha channel is repurposed from a
0/128/255 tri-state flag to continuous depth.** Snow stays reserved at a
flat 255 (unchanged - no gradation needed). Water's real depth is packed
into 1-249 (0 = dry/tie, preserving a clean cutoff), scaled by
`MAX_WATER_DEPTH_FRAC`. `heightMapShader.frag`'s water branch (both GL2/
GL3, kept in sync per this file's convention) now decodes that depth
directly (`(veg.a*255-1)/248`) and feeds it to the unchanged `waterRamp()`
two-stop gradient - this is now genuine simulated depth driving the
visual, not a cosmetic proxy computed from elevation the way pooling's
`WATER_GRADIENT_FLOOR_FRAC` fix was (and incidentally fixes that fix's
own documented limitation for free: a perched pool no longer renders
stuck at the "shallow" shade regardless of its real depth, since there's
no longer a separate perched-pool case at all). `waterLevelFrac` itself
stays as a shader uniform (land vegetation's `landT` still needs it);
only the now-dead `waterGradientFloorFrac` uniform/CPU-side constant was
removed.

**Classification/hysteresis**: `isWaterAt()`/`waterState` keep the exact
same Schmitt-trigger hysteresis mechanism pooling's shoreline-flicker fix
added (`WATER_HYSTERESIS_FRAC`, value unchanged) - same physical sensor-
noise justification applies to a depth signal as much as an elevation
one. The threshold it stabilizes around is now `MIN_VISIBLE_WATER_DEPTH_FRAC`
(new - a cell isn't "visibly water" below this depth, since a real
flowing system can leave a thin near-zero film mid-drain that would
otherwise flicker at a meaningless depth) rather than the old pool/flat
elevation level. `update()`'s per-cell loop reads `waterDepth` as it
stood BEFORE that frame's `stepFlow()` call and `stepFlow()` runs after,
using this frame's freshly-sampled elevation - the same one-frame-lag
ordering `recomputePools()` used, just unthrottled.

**Known first-pass limitations, flagged explicitly (not hidden), per the
approved plan:**
- Numerical stability is more robust by construction than originally
  worried (the self-limiting outflow scale factor prevents negative/
  runaway depth regardless of `FLOW_RATE`), but still genuinely untested
  on real hardware - visual jitter or unexpectedly slow/fast settling is
  still possible and `FLOW_RATE` is the first thing to retune.
- No rainfall/runoff - only the flat sea level acts as a source. A cell
  above sea level stays bone dry until water actually flows there from
  the sea or from digging into existing water. Plausible future addition,
  out of scope here.
- Digging into/mounding over existing water is handled implicitly by the
  per-frame surface-height recompute (raising terrain under water pushes
  it out via the next flow step) rather than a special case - expected to
  look physically right, untested.

**Not yet tested on real hardware at all** - `FLOW_RATE=4.0`,
`MIN_VISIBLE_WATER_DEPTH_FRAC=0.004`, and `MAX_WATER_DEPTH_FRAC=0.05` are
all first-guess defaults (the latter carried over in rough magnitude from
the old `MAX_POOL_RISE_FRAC`), same "untested, live-tunable in the
Vegetation panel" posture every other constant in this file started with.
Verification plan: (1) confirm the flat sea still appears as before at
rest: (2) dig an enclosed bowl above the water line and confirm water
visibly flows in and fills it over a few seconds, continuously, not an
instant snap; (3) mound sand into existing water and confirm it visibly
drains/displaces rather than being trapped; (4) compare FPS with
`ENABLE_FLOW` on vs. off.

## Flagged for later (PhD disclosure-layer phase): SARndbox DEM import/export tool (2026-10)

User shared Bärthele, Sauer & Wilkening (2023), "Exploring the potential
of an Augmented Reality sandbox for geovisualization" (ICC 2023
proceedings) - a practitioner build-and-extend writeup for stock
SARndbox, not a new algorithm paper. Reviewed for whether anything in it
was worth pulling into this fork now; conclusion was mostly no, and
explicitly deferred rather than dropped, for a reason worth keeping:

The paper's standout feature - import a real-world DEM, see a live red/
blue diff overlay against the current sand (too much sand = blue, too
little = red), physically sculpt until it matches, then export the
result back out as a DEM raster - is a disclosure/data-provenance
feature by nature: it brings real data in and discloses the comparison
back to the participant. That's exactly the category the "Strategic
reset" section above assigned to the PhD, not the Master's thesis
(physical/embodied legibility of the core terrain mechanic). So this
is flagged as a concrete, already-proven reference design for that
future disclosure-layer work - a real red/blue diff-against-ground-truth
overlay, not a hypothetical - not something to build now.

The paper's other content (SARndbox/Vrui hotkey input-graph system,
startup automation, a community water-color-cycling plugin) was judged
not worth porting at all, now or later - this project's existing ImGui
panel already covers that same usability ground more directly than
Vrui's key-binding files do, so there's no gap there to fill.

## Final contribution alignment and roadmap toward a mid-December deadline (2026-10-14)

User shared the actual thesis introduction (intro written in finished-
tense: anything it describes as done is a real commitment to have done
by submission, not aspirational framing to soften later) and set a
concrete deadline - **project needs to be finished by mid-December
2026**. This prompted an explicit gap-check of the intro's own
"Contribution" section (1.7) and scope section (1.8) against the actual
codebase, then a deliberate alignment conversation on what the final
contribution will mechanically be, before any of this phase's code gets
written.

**Gap-check findings (codebase as of this date, confirmed by direct
grep, not assumption):** zero hits for "soil" or "moisture" anywhere in
`src/`; zero slope-based vegetation-suitability logic (the only "slope"
hits are from an unrelated, already-removed critter-movement mechanic);
no externalized parameter file of any kind - every constant is a
hardcoded C++ `static float` in `VegetationField.cpp`. So three of 1.7/
1.8's claims describe a system that does not exist yet, not one already
built with different internals:
1. Soil accumulating under established cover and conditioning further
   establishment (the actual two-way feedback loop 1.4 argues the whole
   AR-sandbox field is missing).
2. Suitability evaluated against moisture/slope/soil depth, not just
   the current elevation-band-relative-to-water-line test.
3. A parameter file external to the code, with a named source per
   entry, switchable between a designer preset and a published-ecology
   preset.

Confirmed real and already true, by contrast: the oF 0.9.3->0.12.1
modernization claim in 1.6/1.7 ("recovered codebase... each repair
documented") - `README.md` and `PATCHES.md` substantiate this directly,
addon-patch-by-addon-patch.

**Two forking design decisions resolved explicitly, since they change
both the engineering scope and the strength of the claim the thesis can
honestly make:**

1. **Succession mechanism strength: ACTIVE DISPLACEMENT, the stronger
   textbook-succession claim** - not mere suitability-gating. Once soil
   conditions favor a later-successional species, it must actively
   displace/out-compete whatever earlier-successional species already
   holds that cell, rather than the two simply coexisting once both
   happen to be eligible. This is a real competitive-exclusion
   mechanic, not just "soil changes which band a cell is in."
2. **Species identity: keep the three abstract successional roles**
   (the existing shrub/fruit/nut structure, now explicitly mapped to a
   pioneer/intermediate/climax rank order) rather than renaming them to
   real species directly. Real, specific species and their field-study
   citations back each role's actual numeric parameters in the sourced
   file, but the system and the thesis keep talking in terms of the
   three abstract roles. **User confirmed real species + literature are
   already identified and sourced** - this is not an open research
   dependency, just a data hand-off still pending (see roadmap below).

**This explicitly revives and justifies the previously-shelved
succession idea** (fruit=pioneer/nut=intermediate/shrub=climax,
rank-based displacement) floated earlier in the "Strategic reset"
section above, which had tracked correctly with the user ("yes, that
tracks for me exactly") but was deprioritized in favor of the water-
physics work. It was never rejected, only waiting for something real
(soil depth) to trigger the displacement against - it now has that.

**Finalized contribution statement** (the target to actually build
against, and the wording this project should be judged by at defense
time):

> Shifting Sands is an AR sandbox running a real-time ecological
> succession model as its primary simulation. Vegetation establishes by
> evaluating per-species suitability against three persistent, terrain-
> derived fields - moisture (fed by genuine water flow physics), slope,
> and soil depth. Established vegetation accumulates soil at its own
> location over time; once soil conditions favor a later-successional
> species, that species actively displaces whatever earlier-successional
> species already holds the cell - real competitive exclusion, not
> passive coexistence - so the substrate's own history of what has grown
> on it directly redirects what grows there next. Disturbance destroys
> standing cover and resets the local soil record, so regrowth re-traces
> the same pioneer->intermediate->climax sequence rather than refilling
> uniformly. The three successional roles stay as abstract categories,
> each backed by real named species and field-study citations in an
> external, sourced parameter file, switchable between a designer-set
> preset and the published-ecology one. The whole system rests on a
> documented recovery of an abandoned, unbuildable Magic Sand codebase to
> a current openFrameworks toolchain.

**Explicitly out of scope, so nothing creeps between now and
December:** no disclosure UI (the dual-preset parameter file is real
and inspectable in the GUI, but there's no participant-facing
explanation of it - that stays PhD work per the Strategic reset
section); no rainfall/runoff (water still sourced only from the flat
sea level); no claim about any real place (terrain stays invented, per
1.8's own honest scoping - only the rules are sourced, not the site);
no DEM import/export (flagged for later, see above).

**Roadmap toward mid-December**, phased by dependency:
1. Parameter-file infrastructure (external file via the existing
   `ofXml` convention, per-entry `source` field, designer vs. published
   presets, GUI readout of active preset/source) - unblocked, can start
   immediately, no research dependency since species/citations already
   exist.
2. Hand-off of the user's real species/citation data into that file.
3. Soil depth + moisture + slope fields, and a real suitability test
   against them, replacing the current elevation-band test - the
   critical-path phase; slope is nearly free (reuses the 4-neighbor
   elevation reads `stepFlow()` already computes every frame), soil
   depth and its accumulation-under-cover mechanic are new.
4. The active-displacement mechanic itself (pioneer/intermediate/climax
   rank order, competitive exclusion once soil favors the higher rank) -
   depends on (3)'s soil field existing as a real substrate to act on.
5. Full hardware integration/validation pass - budgeted with real
   contingency, per this project's own track record of needing 1-3
   hardware-found bug-fix rounds on every past feature (CA spread's
   fps regression, pooling's two bugs, flow dynamics still untested).
6. Buffer + thesis writeup, no new features past this point.

Not yet started - this entry records the alignment and plan, not any
implementation. Phase 1 is the agreed next concrete step.

## Phase 1 implemented: EcologyParams sourced-parameter mechanism (2026-10-14)

New `EcologyParams.h`/`.cpp` (`src/Games/`) - a standalone class, not
folded into the already-large `VegetationField`, since this phase is
deliberately just the sourced-parameter/preset-switch MECHANISM, with
nothing in it read by any growth logic yet (that's Phase 3). Keeping it
separate means it's independently inspectable/testable on its own
before anything depends on it, same posture as every other phase of
this project.

**Mechanism**: `EcologyParams::load(Preset)` reads
`bin/data/settings/ecologyParamsDesigner.xml` or
`ecologyParamsPublished.xml` via the same `ofXml`
load/getChild/getValue pattern `SandSurfaceRenderer::loadSettings()`
already established (flat child-element tags, not attributes - kept
deliberately consistent with the one precedent already in this
codebase rather than introducing a second XML idiom). Parses into a
temporary `std::array` first and only commits on full success, so a
missing/malformed role tag can't silently leave a mix of two presets'
data in memory. Three fixed roles - `ROLE_PIONEER`/`ROLE_INTERMEDIATE`/
`ROLE_CLIMAX` - each mapping to channel `fruit`/`nut`/`shrub`
respectively (the mapping decided in the alignment above), carrying a
`speciesName`, a default `source` citation, and three `Range` structs
(moisture/slope/soilDepth), each with `min`/`max`/`unit`/optional
per-parameter source override.

**Units deliberately NOT pre-converted** to this project's internal
0..1 calibrated-range fractions - each `Range` stores whatever unit its
real source material actually reports (degrees, cm, fraction of field
capacity, etc.), so the file stays a literal, checkable transcription
of its citation. Converting into VegetationField's internal
representation is explicitly left to Phase 3, once the real suitability
test exists to do the converting - pre-converting now would mean the
file no longer matches what a reader could verify against the actual
paper.

**Two files shipped, asymmetric on purpose:**
- `ecologyParamsDesigner.xml` - fully filled with internally-consistent
  PLACEHOLDER values (pioneer: wide moisture/slope tolerance, shallow
  soil requirement; climax: narrower tolerance, deep soil requirement)
  so Phase 3/4's suitability and displacement logic has something
  qualitatively sensible to develop against immediately. Every source
  field is deliberately empty/labeled "no published source" - this IS
  the data-free condition, not an oversight.
- `ecologyParamsPublished.xml` - structurally complete but every
  species name/citation/value is an explicit `TODO: Phase 2 hand-off
  pending` placeholder. Deliberately NOT filled with invented citations
  - the user confirmed real species + literature are already sourced
  (see the alignment entry above), so this file is waiting on an actual
  data hand-off, not on research; fabricating a citation here to make
  it look more finished would directly undermine the one property this
  whole mechanism exists to provide (a checkable source per parameter).

**GUI**: `EcologyParams::drawGui()` renders inline into the existing
Vegetation panel (no second ImGui window - matches this project's
established single-panel convention) via a new call at the end of
`VegetationField::drawGui()`. Shows the active preset, buttons to
switch/reload either preset live, and each role's full value+source
readout. Explicitly labeled in its own first lines as "NOT YET READ BY
ANY GROWTH LOGIC" so nobody mistakes this for already-active behavior
during a hardware test - same honesty convention `ENABLE_FLOW`/
`DEBUG_SHOW_SNOW` already use for Phase-ahead-of-itself features.

**Build wiring**: added `src\Games\EcologyParams.cpp`/`.h` to both
`Magic-Sand-Ecosystem.vcxproj` and `.vcxproj.filters`, matching
`VegetationField.cpp`/`.h`'s existing entries exactly - this project's
Visual Studio build lists source files explicitly rather than globbing
the directory, so a new file invisible to the `.vcxproj` would silently
not compile at all despite being a real, correct source file.

**Not yet verified** - this session has no openFrameworks SDK or
compiler available (confirmed: no `ofXml.h` found anywhere on this
machine), so none of this has actually been built. The `ofXml` usage
mirrors `SandSurfaceRenderer::loadSettings()`/`saveSettings()`'s exact,
already-working calls (`xml.load()`, `getChild()`, `getValue()`/
`getFloatValue()`, truthiness checks) rather than anything novel, but a
real Visual Studio build is still the first real confirmation this
compiles and the GUI section renders as intended.

**Next steps**: Phase 2 (species/citation data hand-off into
`ecologyParamsPublished.xml`) can happen independently of anything else
and doesn't block Phase 3. Phase 3 (soil depth/moisture/slope fields +
the real suitability test) is the next engineering phase, and should
consume `EcologyParams::getSpecies()` directly rather than re-deriving
its own copy of these ranges.

## Visualization mode-switch: operator/debug lens now, participant-facing version flagged for later (2026-10-16)

Came out of an aesthetic side-conversation that started with the
Barthele et al. paper's Figure 5 (four alternate height-color-map
presets) and a tangent on jet/Turbo/Inferno colormaps (jet's red-green
adjacency reopens the exact accessibility problem the 2026-09-24
palette pass fixed; Turbo is a smoother, partially CVD-checked
improvement but still a full rainbow with the same structural issue;
Inferno/Magma - from the same `viridis` family that replaced jet as
matplotlib's default - gives the warm "hot glow" quality the user
responded to while staying perceptually uniform and colorblind-safe,
and would be the right pick if that quality is ever wanted here).

**The real idea, once the user reframed it:** Barthele et al.'s four
presets are pure cosmetic palette swaps - same elevation data, different
color lookup table, nothing structurally different about what's being
shown. But the user separately pointed at two systems doing something
different: Ishii's Sandscape (MIT Tangible Media Group) switches between
genuinely different DERIVED quantities computed from the same terrain -
slope (a gradient computation) and shadow (a lighting simulation), not
elevation recolored - and ELF's own separate "Activity Traces" layer
(pollen/magnetometer archaeological survey proxies, per the
Murgatroyd/Butler/Gaffney paper read during the hydrology-phase research
above) is a second, independent simulation switched to, not a reskin of
the ecosystem model. Three unrelated systems converging on "let the
viewer pick which derived field they're looking at" is a real,
established convention in this genre - not a cosmetic nicety, and not
something to conflate with simple palette choice.

**Why this is directly relevant to Shifting Sands right now:** Phase 3
is about to create exactly the kind of multiple distinct per-cell
derived fields this pattern is built for - slope, soil depth, and
moisture, alongside the existing elevation and water depth. Right now
all of this composites into one view (whichever channel wins); there's
no way to isolate just one field. Being able to do that is also
directly useful for confirming the active-displacement succession
mechanic is doing what it should on real hardware - the same validation
role `DEBUG_SHOW_SNOW` already plays for the water/snow boundary.

**Decision: build an operator/debug mode-switch as part of Phase 3
itself** (view slope-only, soil-depth-only, or moisture-only, same GUI
posture as the existing debug toggles) - cheap specifically because it
stays operator-facing: no interaction design, no legibility validation,
no thesis-scope implications, same category as `DEBUG_SHOW_SNOW`.

**Explicitly NOT building, for now: a participant-facing mode-switch.**
User initially asked "should we do both, since the first one is free" -
resolved that the premise doesn't hold: the operator lens is cheap
*because* it's operator-only, and a participant-facing version doesn't
inherit that cheapness - it would need real interaction/legibility
design (how does someone with no domain background know "soil depth
mode" exists or what it means) and would reopen the exact Master's/PhD
disclosure-layer boundary the project deliberately drew to stay
finishable by December. Flagged for later on the same shelf as the
SARndbox DEM import/export tool and the Repast-inspired population-over-
time chart above - a real idea, not rejected, just not this project's
problem before December.

## Terrain/water palette redesign: warm monotonic ramp + water transparency (2026-10-16)

Direct follow-on from the mode-switch conversation above, same visit.
User looked at Barthele et al.'s Figure 5 again and asked to act on two
specific reactions, both against the diverging-ramp background/water
work from 2026-09-24/25: "the one we have, while pretty, is fairly
confusing visually," and two concrete likes from the reference photo -
the top-left panel's hot-white-peak-to-warm-base gradient ("exactly how
I wanted our gradient to behave"), and the top-right panel's water,
which reads as having "some sort of transparency to it; you can see
geological layers under it."

**Diagnosed, not just reskinned: the old 8-stop ramp had the SAME
unreachable-range bug the species channels had before the 2026-09-28
`landT` fix, just never caught.** Tie land (where `terrainRamp()` is
actually used) can only ever render between the water line and the
snow line - identical constraint to shrub/fruit/nut - but the tie
branch was still feeding it raw global `elevationNorm`. So in practice
only some middle slice of the 8 stops (never the deep-blue or dark-
maroon extremes, which require an elevation outside the reachable
band) was ever visible - a real, previously-undiagnosed contributor to
"confusing," on top of the ramp just having a lot of hue changes packed
into that visible slice.

**Redesign: `terrainRamp()` is now a 3-stop MONOTONIC WARM ramp** (deep
rust `#56160a` -> saturated burnt orange-red `#c74d11` -> pale warm
cream `#fae9cc`), replacing the old blue-through-maroon diverging one
entirely, and its tie-land call site now passes `landT` instead of raw
`elevationNorm` - fixing the reachable-range bug at the same time as
the aesthetic change, so the full white-to-red sweep the user liked is
now actually the full sweep a real cell can show, not a cramped middle
slice of it.

**This is also a genuine accessibility improvement, not just a taste
change** - ties directly back to the jet/Turbo/Inferno discussion
earlier the same session. A single hue family swept through lightness/
saturation (no green or cyan stop anywhere) has no red-green adjacency
to collapse under deuteranopia/protanopia in the first place, which is
the exact principle that makes Inferno/Magma a real alternative to
jet - this ramp isn't literally Inferno, but it's built on the same
reasoning, and arguably MORE robust than the diverging ramp it
replaces, not a tradeoff against the earlier accessibility work. Cream
rather than pure white at the peak is deliberate too - keeps the ramp's
own high end visually distinct from actual snow's flat pure white just
above it.

**Water transparency**: `waterRamp()` itself (the two blue stops) is
UNCHANGED - the fix is at the call site. Water's final color is now
`mix(lakebed, waterRamp(waterDepthT), opacity)`, where `lakebed =
terrainRamp(elevationNorm)` (raw elevation, NOT `landT` - landT would
clamp every underwater cell to the same value, losing any sense of a
real lakebed varying with depth) and `opacity = mix(0.3, 1.0,
waterDepthT)` - shallow water lets the terrain-colored "lakebed" show
through, real depth is fully opaque blue, and even the shallowest water
still keeps a 30% blue tint so it never looks identical to bare dry
terrain right at the shoreline.

Implementation: both `heightMapShader.frag` variants (GL2/GL3), kept in
sync per this file's usual convention - `terrainRamp()`'s stops
replaced, its tie-land call site changed to `landT`, and the water
branch in `main()` restructured to blend through `terrainRamp()` rather
than using `waterRamp()`'s output directly. No C++/uniform changes
needed - same `elevationNorm`/`landT`/`waterLevelFrac`/`snowLevelFrac`
already available were sufficient.

Species/agent colors (shrub/fruit/nut peak colors, deer/hunter/
gatherer/fisher) and snow are entirely untouched - this was scoped to
background/tie-land and water only, same scope discipline as every
previous palette pass. Not yet tested on real hardware - same open
question as every other color decision in this project's history.

## Resolved: hand-placed seed dispersal stays deferred, not part of the active plan (2026-10-16)

User asked about `anvaka/fieldplay` (a GPU particle-based vector-field
visualizer, JS/WebGL) as a possible starting point for "the hydrology/
particles part." Clarified it's a flow-visualization technique, not a
fluid solver - the actual water physics already exists (`stepFlow()`),
and particle-trail visualization of it is the literal "non-simulated
decorative flow particles" option the user already passed over in the
Strategic Reset in favor of building real flow dynamics instead. Also a
JS/WebGL tool with no direct path into this C++/openFrameworks/GLSL
pipeline - at best a technique to reimplement from scratch, not
anything to adopt directly.

That prompted a genuinely useful status check, since the original
Liu/ARsandbox014 "three pieces of one methodology" (see the "Future
phase" section above) was water-flow simulation + CA-style competitive
spread + hand-placed seed dispersal, and only two of those three ever
made it into the Dec-14 finalized contribution statement/roadmap.
**Confirmed explicitly: water flow is done** (`stepFlow()`, pending
hardware validation) - **hand-placed seed dispersal is NOT part of the
active plan**, and was quietly absent from the Dec-14 alignment rather
than ever having been formally dropped. User's explicit decision, given
that gap: leave it deferred, same shelf as the DEM tool and population
chart - a real idea from the project's original vision, not rejected,
just not part of what gets built before December.

Vegetation establishment keeps bootstrapping exactly the way it already
does - the existing spontaneous per-tick chance plus the CA spread
layer's same-species neighbor boost (see the "Future phase" section's
point 5: spontaneous growth as the only way to bootstrap "until the
hydrology/seeding layer exists to plant real first seeds" - that
seeding layer is now confirmed to not be coming, so spontaneous growth
stays the permanent bootstrap mechanism, not a temporary stand-in).
Phase 3 gates this same existing mechanism behind real moisture/slope/
soil-depth suitability instead of just the current elevation band -
establishment doesn't need a literal seed object to exist at all, with
or without hand-placed planting.

## GUI palette reference swatch added (2026-10-16)

Two real-hardware tests of the new terrain/water palette (see the
"Terrain/water palette redesign" entry above), both the same day,
failed to show the intended rust->orange->cream sweep - but for two
DIFFERENT geometry reasons, neither a color bug:

1. **First mound (tall, steep)**: peak almost certainly crested past
   the tuned snow line (~+70mm target vs. this box's measured ~+80mm
   max mound height) before `landT` ever reached 1, so the ramp's cream
   stop was skipped over entirely - the white seen was most likely real
   snow (`veg.a > 0.98`), not `terrainRamp()`'s own near-white end.
2. **Second mound (short, wide)**: didn't rise far enough above the
   tuned water line (~-35mm) to traverse much of the ~105mm land band
   at all, so `landT` stayed near 0 almost everywhere visible, pinning
   the whole island to the rust end with no visible orange/cream.
   Established vegetation (teal nut/orange fruit speckle, winner-take-
   all over the ramp per cell) likely also obscured bare "tie" land in
   places, compounding the same effect.

Both are explained by mound SHAPE/HEIGHT relative to the already-tuned
water/snow lines, not by `terrainRamp()`/`waterRamp()` rendering the
wrong colors - but there was no way to tell those two failure modes
apart from a screenshot alone, which is exactly the ambiguity that
caused the misdiagnosis risk.

**User's request, given this: "I just want to see the same colours
visuals that are shown in [Barthele et al.]. Once we work out the
proper parameters, we can change things up."** - i.e., confirm the
PALETTE itself matches the Figure 5 reference look right now,
independently of getting real sand geometry/calibration to reveal it,
and defer the actual physical-parameter tuning (mound shapes,
water/snow line retuning, etc.) to a separate later pass.

**Added: a palette reference swatch in the Vegetation panel**
(`VegetationField::drawGui()`, right after the panel's intro text) -
draws `terrainRamp()`'s three stops (rust `#56160a` -> orange `#c74d11`
-> cream `#fae9cc`) and `waterRamp()`'s two stops (`#00429d` ->
`#4b73a2`) as literal ImGui gradient bars via
`AddRectFilledMultiColor()`, explicitly labeled "NOT live sand data."
This is pure reference - it reads no elevation/vegetation state at all,
so it answers "does this software have the right colors" completely
independently of "did I sculpt the right terrain shape to reveal them
live" - the exact two questions the two failed tests above conflated.

**Deliberately NOT done as part of this change**: no retuning of
`TEMPERATURE`/`LIVING_RANGE_FRACTION`/the water-snow band width, and no
attempt to make small mounds show more gradient automatically (e.g. by
widening the land band) - both are exactly the "proper parameters" the
user explicitly asked to defer. Those already-measured values
(water -35mm/snow +70mm, see the hydrology-phase entries above) stay
as-is; this change only adds a way to verify the palette in isolation,
not a way to make it easier to hit on real sand.

**Colors are hardcoded in C++, duplicating the shader's own GLSL
constants** - called out explicitly in the code comment as a value that
must be hand-kept in sync with `heightMapShader.frag`'s `terrainRamp()`/
`waterRamp()` (both GL2/GL3 variants) if either is ever retuned again,
since there is no shared source of truth between GLSL and ImGui in this
codebase's current structure.

Not yet tested on real hardware - this is a pure GUI addition with no
dependency on the Kinect/vegetation pipeline, so the risk of it not
rendering correctly is low, but ImGui's `AddRectFilledMultiColor()`
usage here is new to this codebase and worth a quick visual check on
the first run after pulling.

**Next step for the user once this is confirmed to look right**: if the
swatch itself matches the Figure 5 reference look, that closes the
"are the colors right" question for good - what's left is purely the
physical-parameter work already flagged as deferred (sculpting a mound/
slope tall and gradual enough to sweep the real band without crossing
the snow line, and/or revisiting whether the current water/snow line
targets leave enough room to do that comfortably on this box).

## Real root cause found: vegetation speckle, not color or geometry (2026-10-16)

Same visit, immediately after the swatch above. User tested on real
hardware and reported the live sandbox is "nowhere near the reference"
- with a screenshot showing exactly what the two prior mound tests
showed: a big raised landform almost entirely covered in dense teal/
orange speckle (established nut/fruit vegetation), with only slivers of
bare rust/brown visible between speckles, plus the surrounding water.

**This is the actual root cause, not a third geometry variant of the
same mystery.** Barthele et al.'s Figure 5 images are a bare DEM
colormap - elevation only, zero vegetation layer. This project ALWAYS
composites species vegetation on top of the terrain ramp, winner-take-
all with no fade (a deliberate, already-validated-on-hardware design
choice from the 2026-09-24 accessibility pass - see that entry's
"Explicit design question raised and resolved" note: the pixelated-CA
look specifically depends on vegetation having zero blend with
terrainRamp()). On real, previously-used sand, most land already has at
least trace vegetation density from normal use, so most of what's
visible is species speckle, not the elevation ramp at all - structurally
incomparable to a bare-DEM reference image, no matter how correct the
ramp's own colors or the sculpted terrain's shape are. The prior two
"geometry" explanations (snow cresting; band not swept) were real and
still correct for what they each showed, but neither was the dominant
effect in THIS screenshot - vegetation coverage was.

**Fix: `DEBUG_HIDE_VEGETATION`** (`VegetationField.h`/`.cpp`, default
false) - a new debug-only toggle, same category/wiring pattern as
`DEBUG_SHOW_SNOW`. When enabled, `heightMapShader.frag`'s three species
branches (shrub/fruit/nut) are skipped entirely regardless of actual
density, falling through to the existing tie-land `terrainRamp(landT)`
branch - water and snow are deliberately left alone (Figure 5's own
images still show water/relief, just no vegetation overlay, so hiding
water/snow too would no longer match the reference either). This gives
a real, direct, vegetation-free comparison against the reference image
for the first time, cutting the ambiguity between "wrong colors," "wrong
geometry," and "vegetation is covering it" that the last three rounds
of real-hardware screenshots couldn't distinguish from each other.

Checkbox added in the Vegetation panel directly below the palette
swatch from the entry above, with inline text explaining why this
specific toggle is the right next diagnostic step. Purely a debug render
path - does not touch actual growth/establishment logic, species
densities, or any saved state.

**Both GL2/GL3 shader variants updated in sync** per this file's usual
convention; `SandSurfaceRenderer::drawSandbox()` sets the new uniform
the same way `debugShowSnow` is already set.

Not yet tested on real hardware. Expected result once pulled: toggling
this on should make the raised landform in the user's last screenshot
show clean rust->orange->cream bare terrain (matching the GUI swatch),
confirming the colors and (depending on whether the band gets swept)
the geometry were likely fine all along, and the "nowhere near the
reference" impression was specifically the vegetation layer. If toggling
it on still doesn't produce a clean gradient, that would point back at
geometry (snow cresting / band not swept, per the entry above) as a
real, separate, still-open issue rather than vegetation coverage.

**CONFIRMED on real hardware (2026-10-16), same visit - the real
obscuring factor was BOTH vegetation AND agent population icons, not
just vegetation.** Before testing `DEBUG_HIDE_VEGETATION`, the user's
screenshot was re-examined for color: the dense teal/mint cluster and
scattered colored squares matched `Critter::BODY_COLOR` (deer,
`(0,190,255)`), `HumanAgent::GATHERER_COLOR` (`(255,190,0)`), and
`HumanAgent::FISHER_COLOR` (`(30,30,130)`) almost exactly - a
completely separate rendering path (`CritterController`/`HumanAgent`
draw calls) that `DEBUG_HIDE_VEGETATION` has no effect on at all, since
that toggle only gates `heightMapShader.frag`'s vegetation-texture
branches. This means the earlier "vegetation speckle" diagnosis in the
entry above was likely incomplete - probably correct for the fruit/nut
texture also present, but not the dominant effect in that particular
screenshot, which was most likely a dense deer herd plus nearby human
agents rendered as discrete icons on top of everything else.

User combined BOTH fixes using the project's own existing "Population"
panel controls (`CritterController.cpp` - "Remove all deer"/"Remove all
humans" buttons, already established precedent from the 2026-09-22
grazing-pressure test) alongside `DEBUG_HIDE_VEGETATION`: zeroed both
populations AND hid vegetation. Result: the live sandbox now visibly
matches the Figure 5 reference composition - blue water, a smooth
rust->orange->cream raised landform with a real gradient, no speckle.
**This closes the "doesn't match the reference" investigation** - the
palette (`terrainRamp()`/`waterRamp()`) and the underlying elevation
geometry were correct the whole time; every prior failed comparison was
agents and/or vegetation sitting on top of a working ramp, not a color
or shader bug.

**One remaining anomaly, NOT YET EXPLAINED, flagged as open/low-priority
rather than silently dropped:** a solid, hard-edged orange blotch in
the bottom-left of the confirming screenshot, visually distinct from
the smooth gradient everywhere else - blocky/speckled rather than
continuous. Ruled out so far: not water (not blue), not snow (not
white), not contour lines (those render flat black, confirmed earlier
in the fidelity audit), not vegetation or agents (both off for this
test). Most likely either (a) a real, distinct physical sand feature at
roughly mid-band elevation that genuinely should render solid orange,
or (b) photo/sand-texture/projector-moire noise if the screenshot is a
phone photo of the physical install rather than a screen capture,
rather than a code issue - asked the user directly whether that area
has a real distinct sculpted shape to it before assuming it needs a
code fix, per this file's standing rule of measuring before guessing.
Not blocking - the main investigation is resolved regardless of how
this one patch turns out.

## Literal Figure 5 colormap match (2026-10-16)

Despite confirming the palette AND geometry were both structurally
correct (vegetation/agent speckle was the real obscuring factor, see
above), the user still said directly: "I just want it to look like the
reference. Here is the paper if you need to look at it again" - a clear
signal that the actual target had never been correctly identified, not
that something else was still broken.

**Root cause: this session's own earlier description of Figure 5 was
wrong, carried forward from a verbal paraphrase rather than the actual
image.** Re-opened the PDF (`pdftoppm` at 300dpi) and sampled real
pixel RGB values along a radial line from the top-left panel's edge to
its peak (`Pillow`, `python3`), rather than re-describing it from
memory a third time. Result: it is NOT a single warm hue family at all -
it's a full rainbow/hypsometric colormap: **teal/cyan (lowest) -> green
-> yellow/olive -> orange-brown -> dark maroon -> pale near-white
(peak)**. This matches the paper's own body text exactly (section 3:
"the basic map shows low-level terrain in green and blue, and higher-
level terrain in red and white") - it's SARndbox's own STOCK default
height colormap, not a custom design at all. The earlier "hot white
peak to warm base, single warm hue" description that the 3-stop
rust->orange->cream ramp was built from was simply inaccurate - it
captured "white peak" correctly but invented a warm-only structure the
reference image never had.

Also re-examined the top-right panel's water, which the user separately
praised for "transparency... geological layers under it": this turned
out to be contour lines visibly continuing under/through the water, not
a translucency blend with the ground color beneath - a different
mechanism than the `mix(lakebed, waterRamp(...), opacity)` blend
implemented for that feature, though structurally close to it already
working for free (this project's contour-line pass already draws over
water unconditionally).

**Direct conflict surfaced to the user before implementing, not
silently resolved either way:** literally matching Figure 5 means a
real green-to-yellow-to-orange-to-red progression - the exact
worst-case adjacency for red-green colorblindness that the 2026-09-24
accessibility pass deliberately eliminated project-wide. Put to the
user via `AskUserQuestion` with three options (literal match / a
colorblind-safe approximation keeping the same arc shape / handle
per-element). **User chose literal match explicitly**, accepting the
accessibility tradeoff for this specific visual as an informed,
deliberate choice - not an accident or an oversight this time.

**Implementation: `terrainRamp()` rebuilt as a 6-stop piecewise-linear
ramp**, stops taken from the actual sampled Figure 5 values (cleaned up
to round, deliberate numbers rather than raw noisy samples):
teal `(0,141,167)` -> green `(64,179,89)` -> yellow/olive `(204,199,77)`
-> orange-brown `(191,115,38)` -> dark maroon `(115,51,40)` -> pale
near-white `(230,230,230)`. Both `heightMapShader.frag` variants
(GL2/GL3) updated in sync per this file's usual convention - function
signature and all call sites (tie-land using `landT`, water's lakebed
read using raw `elevationNorm`) are UNCHANGED, only the stop colors and
interpolation logic inside `terrainRamp()` itself changed, from 3 stops/
1 kink to 6 stops/4 kinks. `waterRamp()`'s own two blue stops are
UNCHANGED - the user's two likes (gradient behavior, water look) were
about different things, and the water "transparency" turned out to be
contour lines, not a color problem to re-solve here.

**Explicitly scoped**: species/agent colors (shrub/fruit/nut peak
colors, deer/hunter/gatherer/fisher) are completely untouched and still
use the Okabe-Ito-derived, colorblind-safe palette from the 2026-09-24
pass - this tradeoff was deliberately confined to background/tie-land
only, the one element the user was actually comparing against Figure 5,
not applied project-wide by default.

**GUI palette reference swatch (added earlier the same day) updated to
match** - redrawn as a 6-segment gradient bar using the same sampled
stop colors, with new inline text explicitly flagging that this is now
a literal rainbow match and a deliberate departure from the earlier
colorblind-safe design, so the GUI doesn't silently drift out of sync
with what the shader actually renders (same "hand-kept in sync, no
shared source of truth" caveat as when the swatch was first added).

Not yet tested on real hardware - this is the third iteration of this
same ramp in one day, so same open question as every prior palette
pass, but now at least confirmed to be targeting the CORRECT reference
image this time, sampled directly rather than paraphrased.

## Freeze called on visual churn, moratorium pending one clean verification pass (2026-10-19)

User raised a direct worry: "I'm worried that these recent updates have
caused too many issues. We had a working ELF port before" - prompted by
three terrain-ramp redesigns in one sitting (3-stop warm ramp -> literal
6-stop Figure 5 rainbow) plus a confusing real-hardware screenshot (a
blurry brown-only patch, no green/yellow) that hadn't been explained yet.

**Checked precisely rather than reassuring from memory** (`git diff
1bbad8a HEAD --stat` and a line-level diff of the agent files):
`CritterController.cpp`/`.h` (update order, spawn/death, population
caps) have ZERO diff since the confirmed-working checkpoint.
`Critter.cpp`/`HumanAgent.cpp` have 12 and 31 changed lines respectively,
and every one of them is a color CONSTANT from the 2026-09-24
accessibility pass - movement, eating, fishing, hunting, growth, and
spawn/death logic are byte-identical to the checkpoint. So the actual
ELF port the user is worried about losing was never touched by any of
this - every real change since the checkpoint is confined to
`VegetationField.h`/`.cpp`, `heightMapShader.frag` (GL2/GL3),
`EcologyParams.h`/`.cpp` (new, still inert - nothing reads it), and
`SandSurfaceRenderer.cpp`'s uniform wiring - the succession-model LAYER
that sits alongside the ELF port, not inside it.

**User's explicit decision, given this, via `AskUserQuestion`** (three
options: stabilize current state / roll back only the palette to plain
ELF-stock colors / full revert to `1bbad8a` discarding the whole
succession-model phase): **chose to stabilize** - no revert, no further
rollback. Rejected options carried real costs worth recording: a full
revert would have discarded the already-tuned CA spread layer, the
flow-dynamics rewrite, and reset the entire Dec-14 thesis roadmap this
phase is the foundation of, for a problem that turned out to be visual-
layer churn, not simulation regression.

**Moratorium, effective now: no further terrain/water palette changes
until ONE clean, measured verification pass confirms or refutes the
current state** - the opposite failure mode from today's 3-redesigns-
in-one-sitting pattern (each change made before the last was actually
confirmed). Verification checklist (reuses existing diagnostics, no new
code):
1. `git log -1` already confirmed at `722f4c1` on hardware.
2. Zero out population (Population panel - "Remove all deer"/"Remove
   all humans") and enable `DEBUG_HIDE_VEGETATION`, exactly as the
   2026-10-16 confirmation test did, to isolate bare terrain again.
3. At the lowest and highest point of any raised/dug feature, read
   "Raw elevation at ROI center" against the "Water line"/"Snow line"
   mm readout - confirms whether that feature actually spans enough of
   the band to show the ramp's low (teal/green) end, or whether (as
   suspected for the blurry brown-only screenshot) it only occupies the
   upper orange/maroon/white portion.
4. Compare FPS with `ENABLE_FLOW` on vs. off - the flow-dynamics
   rewrite's own still-open verification item, not yet done.
5. Only after all four return clean, confirmed answers does any further
   palette tuning resume - and the next change should be singular and
   verified before a second one starts, not stacked.

Not yet executed - this entry records the decision and the checklist to
run, not a completed verification.

**EXECUTED (2026-10-19), same visit - verification checklist items 1-3
all confirmed clean, closing the "blurry brown-only patch"/"is ELF
messing with the gradient" worry with real numbers, not another
guess.** User sent two Vegetation-panel + sandbox screenshot pairs
(not a hand in frame - "ROI center" is a fixed calibrated point, not
wherever a hand is; clarified this before the dump since the earlier
instruction read as asking for that).

Pair 1: raw elevation at ROI center = **83.4mm**, against Water line
-34.0mm / Snow line 67.9mm - ABOVE the snow line, confirmed
`snow=yes` in the panel's own readout. The tiny white marks visible at
the exact center of the brown blob in the matching sandbox screenshot
are that real snow classification, not a rendering artifact. The
blob's surrounding flanks (visually ~40-65mm by the same scale) work
out to `landT` (the ramp's 0-1 position, `(elev-waterLevelFrac)/
(snowLevelFrac-waterLevelFrac)`) of roughly **0.73-0.97** - deep in the
6-stop ramp's orange->maroon->near-white tail, nowhere near the
teal/green/yellow end (`landT<~0.4`). This fully explains the "no
green or yellow anywhere" symptom from the prior entry: not a missing
color, a mound whose entire visible surface never drops low enough in
the band to reach them.

Pair 2: raw elevation at the same ROI center, resculpted, = **-16.5mm**
- close to the water line, `landT≈0.17`, the ramp's teal/green zone.
The matching sandbox screenshot visibly shows MORE yellow/olive at the
edges than pair 1's did - **direct, same-session, same-build proof
that the ramp renders its full range correctly** when a cell's real
elevation actually sits in that part of the band. This closes the
loop: the 6-stop ramp was never broken, every real-hardware "doesn't
match" screenshot across this whole multi-day thread has been
explained by mound/dig GEOMETRY (snow cresting, band not swept,
vegetation/agent coverage) rather than a color or shader defect - and
this is the first time two readings in the SAME session, on the SAME
build, show both ends of the ramp actually rendering as designed.

**Still open, not yet executed:** checklist item 4 (FPS with
`ENABLE_FLOW` on vs. off) - the flow-dynamics rewrite's own
still-outstanding hardware verification, unrelated to the palette
question this closes out. Asked the user whether to run it now or
leave the moratorium satisfied without it.

**Practical takeaway for sculpting, now confirmed rather than guessed:**
to see the ramp's full teal->green->yellow->orange->maroon->white sweep
on one feature in view at once, build a mound/slope that starts near
the water line (-34mm on this box) and rises gradually to just under
the snow line (67.9mm), rather than a short steep peak (crests into
snow early) or a mound that stays entirely in the upper band (stuck in
orange/maroon, this thread's actual recurring case).

## New branch split: ss/terrain-reference-visuals (2026-10-19)

Same visit, right after the verification pass above. User looked again
at the confirming screenshots (vegetation hidden, population zeroed)
and pushed back hard, correctly: "It's blotchy, it's all water. The
water itself isn't transparent at all. On the hydrology side, there is
no flow." This was a real correction to an overconfident claim just
made - today's verification pass confirmed `terrainRamp()`'s COLOR MATH
renders correctly across its range, which is a narrower claim than "the
hydrology/visual system is working." Specifically NOT verified by
anything done today:
- Water/land shoreline distinctness - the new 6-stop ramp's low stop
  (teal `(0,141,167)`) is a blue-family color sitting right next to
  real water's blue, unlike the old 3-stop ramp's rust low stop which
  was unambiguously land-colored. Plausible reopening of the exact
  problem the 2026-09-25 "water needs its own distinct gradient" fix
  solved once already.
- Water transparency actually being VISIBLE - the `mix(lakebed,
  waterRamp(waterDepthT), opacity)` blend exists in code, but
  `opacity` saturates toward 1.0 (fully opaque) as `waterDepthT` rises,
  and nothing has confirmed what depths are actually being reached in
  practice - if most visible water sits near `MAX_WATER_DEPTH_FRAC`,
  it would look solid and flat exactly as reported, with the
  transparency code path real but practically unreachable.
- Flow dynamics BEHAVIOR - today's entire verification was static
  screenshots. The original hydrology-rewrite verification plan's four
  real steps (sea stays stable at rest; dig a bowl, watch water flow in
  over several seconds; mound into existing water, watch it displace;
  FPS with `ENABLE_FLOW` on/off) have STILL never been run, on any
  build, at any point in this project's history. "There is no flow" may
  be literally true and simply never caught, since nothing has looked
  for motion over time before.

**User's decision, given this: branch split, not another fix-in-place
round.** `ss/ecosim-model-visuals-kiqw12` (the branch this whole
multi-day thread happened on) is explicitly left untouched - still the
full ELF-port-plus-succession-model branch, still has the Dec-14
roadmap's foundation (EcologyParams, CA spread, flow dynamics) intact.
A new branch, `ss/terrain-reference-visuals`, branched from the same
HEAD (`1647872`), is where the Figure-5-reference visual work
continues - explicitly **with no ELF simulation visuals at all**
(vegetation species speckle, agent icons), not toggled off but
defaulted off, so every observation on this branch is automatically a
clean bare-terrain/water comparison without needing to remember two
separate manual steps (hide vegetation, zero population) before every
single hardware test - exactly the repeated friction that made today's
diagnosis slower than it needed to be.

**Two default-only changes, both still fully reversible through
existing GUI controls, nothing removed:**
1. `VegetationField::DEBUG_HIDE_VEGETATION` default changed `false` ->
   `true`. Still a live checkbox in the Vegetation panel if vegetation
   state ever needs inspecting on this branch.
2. `CCritterController::setup()`'s `deerSpawnCount`/`humanSpawnCount`
   changed `5`/`5` -> `0`/`0`. These feed BOTH the automatic spawn on
   first valid Kinect ROI (`setKinectROI()`'s "if (firstValidROI)"
   block - confirmed this is the actual auto-spawn site, unconditional,
   every launch) and the Population panel's manual "Add deer"/"Add
   humans" button counts - `addDeer(0)`/`addHumans(0)` are silent
   no-ops, so startup now adds zero agents, but the Population panel's
   sliders/buttons still work exactly as before if agents are ever
   wanted on this branch.

**Explicitly NOT done**: no deletion of any ELF simulation code,
agent logic, or vegetation growth mechanism - only what's drawn/spawned
by default changed. The simulation keeps running underneath exactly as
before; this branch just doesn't surface it visually unless asked to.

**Next steps on this branch, not yet done**: the three real gaps
identified above (shoreline color distinctness, water opacity/
transparency reachability, and actually running the flow-dynamics
behavioral test for the first time) still need real investigation -
this branch split only removes the vegetation/agent confound, it
doesn't yet fix any of the three. Per this project's standing "measure
before tuning" rule, the next concrete step is adding a live water-
depth readout (same pattern as the existing elevation readout) so
`waterDepthT`/opacity can be read as a real number instead of inferred
from a screenshot, before touching any color or opacity math again.

## Reverted to stock Magic Sand height-colormap rendering (2026-10-19)

Same visit, right after the branch split above. User corrected the
scope directly: the two default-only changes (hide vegetation, no
agent auto-spawn) weren't actually what was wanted. The real ask: "go
back to before ELF -- to a version that looks more like the one in
[Barthele et al.]. Read the paper again, they do explain how they got
the visuals they did."

**Re-read the paper's actual methodology section (not re-paraphrased)
and found the exact mechanism, named explicitly:** section 4.2 states
the colors in SARndbox's projected height map are "defined in a cpt-
file containing four columns: The first one describes the end of a
height interval, while the other three contain an RGB value for that
interval," and that "the name of the file must remain 'HeightColorMap'
to be correctly referenced." This is a literal height-interval color-
key lookup table, nothing more - no custom shader math, no vegetation
overlay, no species classification. Section 3's own description of the
STOCK/default appearance ("low-level terrain in green and blue, higher-
level terrain in red and white") is this exact mechanism's default
output, which is also what Figure 5's four panels are variations of.

**This mechanism already existed in this codebase, completely intact,
the entire time - inherited unmodified from the original Magic Sand
base this project forked from.** `bin/data/colorMaps/HeightColorMap.xml`
is a height-interval key table in exactly this format (-220mm deep blue
through 0mm teal/green through 60-90mm brown/red through 140-220mm
gray/white) - never deleted, never even edited by any commit in this
project's history. `SandSurfaceRenderer::loadSettings()` still loads it
into `heightMap` every run, and `drawSandbox()` still sets
`heightColorMapTransformation`/`heightColorMapSampler`/
`heightMapNumEntries` as real shader uniforms every single frame -
confirmed by direct grep, not assumption. The ecosystem-fork shader's
own header comment even said so out loud: "heightColorMapSampler is
left bound but unused below, kept only so the existing colormap-editing
GUI machinery doesn't need to be torn out to keep this compiling." The
data pipeline was never broken or removed - the fragment shader just
stopped reading from it once `terrainRamp()`/`waterRamp()`/species
logic took over as the primary color source, starting with `abb8fe3`
("Bring back topology with an organic growth look, not the rainbow
ramp") - note that commit's own message confirms a plain height-
colormap "rainbow ramp" is literally what used to run here, before ANY
of this fork's custom visual work, ELF included, ever began.

**Fix: found `abb8fe3`'s parent commit (the literal last version before
any of this fork's custom shader work started) via `git show
abb8fe3^:<path>`, and restored both `heightMapShader.frag` variants
(GL2/GL3) to that exact code, byte-for-byte** - not a reconstruction
from memory or re-derived logic, the actual historical file. Core of
both: `vec4 color = texture2DRect(heightColorMapSampler, vec2(depthfrag,
0.5));`, then the same contour-line pass already used everywhere else
in this project, unchanged. All ecosystem-specific uniforms (vegetation
sampler, water/snow uniforms, debug toggles, `terrainRamp()`/
`waterRamp()` themselves) are gone from these two files entirely on
this branch.

**Verified safe before committing, not just hoped correct:**
1. Checked the VERTEX shader (`heightMapShader.vert`, both GL2/GL3)
   diffs against the same pre-fork commit - the only change in either
   is the ecosystem fork's addition of `texcoordfrag` (for the now-
   removed vegetation sampling), left harmlessly unused by the restored
   fragment shader. `depthfrag`'s own computation
   (`elevation*heightColorMapTransformation.x+heightColorMapTransformation.y`)
   is completely unchanged, so the restored fragment shader samples the
   exact same texel-space coordinate the original code always expected
   - no risk of silently sampling the wrong location.
2. Confirmed no C++ changes are needed: `ofShader::setUniform*()` calls
   for uniforms a shader no longer declares are standard, silent no-ops
   in openFrameworks (`glGetUniformLocation` returns -1, oF checks for
   that before calling `glUniform*`) - `SandSurfaceRenderer.cpp`'s
   `drawSandbox()` still calls `setUniform1i("hasVegetation", ...)` etc.
   every frame, completely harmlessly, since those uniforms simply don't
   exist in this branch's shader program anymore.

**Net effect**: this branch's visual output should now be the literal,
unmodified stock Magic Sand / SARndbox-equivalent height-colormap
rendering - the real "before ELF" state, not an approximation of it -
using the exact mechanism the reference paper describes, with the
inherited `HeightColorMap.xml` stops already in a plausible blue-green-
tan-brown-red-gray-white hypsometric progression reasonably close to
Figure 5's own look. `VegetationField`'s simulation (growth, agents,
flow dynamics, EcologyParams) keeps running completely unaffected
underneath - nothing about the simulation itself was touched, only
which shader file gets compiled on this branch.

**Not yet tested on real hardware** - same open status as every other
visual change in this project's history, but this one carries unusually
low risk given it's a restoration of code that demonstrably ran
correctly before, not a new design.

**Deliberately left open, worth a real decision later, not decided
here:** whether to keep real simulated water DEPTH (from `stepFlow()`)
visually represented at all on this branch - right now "water" is
whatever `HeightColorMap.xml`'s own low-elevation blue stops naturally
render for any sufficiently low cell, which is a static elevation-only
proxy, not the actual simulated depth. This matches the reference
paper's OWN basic/default appearance (its water-reactive demo, Figure
4, uses a separate hand-triggered flooding tool, not the basic height
map), so it's consistent with "look more like the reference," but it
does mean this branch's water is currently just elevation-colored
terrain, same as every other low spot - not wrong for this branch's
stated goal, just worth being explicit about.

## HeightColorMap.xml recolored, colorblind-safe (2026-10-19)

Same visit, immediately after reviving the stock mechanism above. User
asked to work the branch's open items one at a time, starting with
color, and clarified directly: wants a colorblind-safe alternative, not
the stock file's own inherited (not colorblind-checked) blue-green-tan-
brown-red-gray-white stops.

**Reused the already-validated palette rather than inventing a new
one** - the 2026-09-24 accessibility pass's 7-stop diverging ramp
(`#00429d,#5c83a6,#a5c2bd,#ffffe0,#ffa59e,#dd4c65,#93003a`), explicitly
sourced by the user and confirmed colorblind-safe at the time, just
never previously applied to THIS mechanism (it was built for the
ecosystem fork's now-removed GLSL `terrainRamp()`). Reusing it keeps
this project consistent with its own prior accessibility decision
instead of re-litigating a new palette from scratch.

**Implementation: replaced `HeightColorMap.xml`'s 16 key colors via
smooth linear interpolation through the 7-stop ramp**, keeping every
other structural element of the file identical - same 16 height
breakpoints (`-220mm` through `220mm`), same count, same water/land
split at `0mm`. Each key's height was converted to `t =
(height+220)/440` (0 at the floor, 1 at the ceiling) and the new RGB
value taken from the piecewise-linear ramp at that `t`. Net look: deep
blue `(0,66,157)` at the lowest water, warming through pale cream
`(255,255,224)` right around the water's edge (0mm), through salmon/
rose, to dark maroon `(147,0,58)` at the highest peak - monotonic
blue-to-red through a light midpoint, no green stop anywhere to
collide with red under deuteranopia/protanopia.

**Not a new validation pass** - this is the SAME palette already
checked once (2026-09-24 entry), just resampled at new points via
linear interpolation. Interpolating between colorblind-safe endpoints
is a reasonable, low-risk assumption (no point on a monotonic blue-
pale-red path through this specific 7-stop sequence introduces a new
green/red collision), but wasn't independently re-run through a
simulator - flagged honestly, not re-asserted as freshly verified.

Only `bin/data/colorMaps/HeightColorMap.xml` changed - no shader or
C++ changes needed, since the restored stock mechanism already reads
this file's colors directly as its sole color source. Not yet tested
on real hardware.

## HeightColorMap.xml recolored AGAIN - the diverging ramp was too desaturated (2026-10-19)

Same visit, immediately after. User pushed back directly: "This is way
too desaturated, at least for someone who isn't colourblind. What is a
better alternative? Grass should be some shade of green, water should
be some shade of blue, snow should be some shade of white. The rest of
the gradient is flexible." A real, specific correction - the diverging
ramp above sacrificed ordinary legibility (vivid, intuitive color-to-
feature mapping) for accessibility, when both were achievable at once.

**Re-derived the actual accessibility lesson from this project's own
history rather than over-applying "avoid green."** The real hazard,
confirmed back in the 2026-09-24 accessibility pass, was never "green
exists" - it was fruit's PURE SATURATED RED sitting directly next to
shrub's PURE SATURATED GREEN, the textbook red-green worst case. Blue,
green, tan, brown, gray, and white are all safely distinguishable from
EACH OTHER under deuteranopia/protanopia - it's specifically a
saturated red near a saturated green that collapses. So real grass-
green, real water-blue, and real snow-white are all fine on their own;
the only real constraint is keeping saturated red out of the ramp
entirely (brown/tan/gold instead), which a classic hypsometric terrain
tint does naturally anyway.

**New design, vivid rather than muted:**
- **Water** (8 keys, -220mm to -0.5mm): ColorBrewer's "Blues" sequential
  scale, a standard pre-validated colorblind-safe blue ramp - real
  saturated navy at depth (`#08306b`) lightening toward the shore
  (`#deebf7`), not desaturated grayish blue.
- **Grass/shoreline** (0mm/2.5mm): rich forest green `#1b7837` ->
  yellow-green `#4d9221`.
- **Flexible middle** (25-140mm): olive `#a6bd22` -> tan `#dfc27d` ->
  brown `#bf812d` -> darker brown `#8c510a` - real soil/rock tones, the
  "flexible" part of the request, deliberately avoiding any saturated
  red stop anywhere in the sequence.
- **Rock/snow** (200mm/220mm): light gray `#bfbfbf` -> white `#ffffff`.

Same 16 height breakpoints as both prior versions, unchanged - only
the RGB values at each key changed again. No shader/C++ changes needed,
same as the previous recolor - `HeightColorMap.xml` is still the
mechanism's sole color source.

**Not independently re-run through a colorblind simulator** - the
"no saturated red anywhere in the ramp" rule is reasoned from this
project's own prior confirmed finding (the fruit/shrub collision was
specifically a red-green pair), not a fresh validation pass. Flagged
honestly, same caveat as the previous color iteration. Not yet tested
on real hardware.

## HeightColorMap.xml recolored A THIRD TIME - user-supplied 7-stop palette with per-stop accessibility rationale (2026-10-19)

Same visit, immediately after. User supplied a specific 7-color table,
each with a named zone and explicit reasoning, rather than asking for
another from-scratch design pass:

- Deep Water `#08306B` (Maximum Depths) - "highly saturated dark navy
  blue that provides a strong baseline anchor."
- Shallow Water `#41B6C4` (Coastal Lines/Lakes) - "bright cyan-teal
  blue. Shifting toward teal makes it distinct from deep water and
  land for all vision types."
- Lowland Grass `#238B45` (Basins/Valleys) - "deep forest green. It
  acts as a stark, high-contrast border against the bright shallow
  water."
- Foothill Grass `#74C476` (Mid-Elevations) - "soft sage green. By
  keeping this lighter than the valley green, elevation change is
  communicated via brightness."
- High Highlands `#D95F02` (Steep Slopes) - "muted clay/terracotta
  orange. Crucial alternative to brown: brown turns into mud-green for
  red-green colorblind users, whereas this orange remains perfectly
  distinct."
- Mountain Peaks `#FDBE85` (Sub-Alpine Ridges) - "soft, sandy
  cream-orange that bridges the gap between high rock and snow fields."
- Snow Caps `#F7F7F7` (High Peaks) - "off-white. Never use pure white
  for snow, as it can conflict with standard map [elements]."

**Mapping onto the file's existing 16 height breakpoints**: the 7 named
zones were placed at the 7 existing keys whose real-world role matches
each name most directly - `-220:Deep Water`, `-2.5:Shallow Water`,
`2.5:Lowland Grass`, `60:Foothill Grass`, `140:High Highlands`,
`200:Mountain Peaks`, `220:Snow Caps` - so each is an EXACT, unmodified
copy of the user's own hex value at that key, not a re-derived
approximation. The remaining 9 keys (`-200,-170,-150,-125,-7.5,-0.5,0,
25,90`) were filled by linear RGB interpolation between whichever two
named anchors they fall between by height - the same piecewise-linear
technique used for every previous recolor of this file, just applied to
7 non-uniformly-spaced anchors instead of a uniformly-spaced ramp.
Net effect: deep navy at the floor, warming through teal at the
shoreline, a real green transition right at the 0mm water line (teal
`(53,165,145)` at -0.5mm to green `(50,161,133)` at 0mm, continuing into
grass `(35,139,69)` at 2.5mm) that gives a crisp, high-contrast land/
water edge per the user's own stated intent for that pairing, then
green through two foothill shades, an olive transition at 90mm, into
the user's orange/peach highland-to-peak stops, ending in off-white
snow.

**Not a redesign - a direct implementation of user-supplied, already-
reasoned values.** No new accessibility analysis performed on top of
the user's own rationale; the interpolation between their anchors is a
mechanical fill of the structural gap (7 named colors vs. 16 required
breakpoints), not a judgment call on color choice. Same caveat as every
prior iteration: not independently re-run through a colorblind
simulator, and not yet tested on real hardware. No shader/C++ changes
needed - `HeightColorMap.xml` remains the sole color source for this
branch's reverted stock rendering mechanism.

**REAL ROOT CAUSE FOUND same day, once this version was actually seen
on hardware: the problem was never the colors at any of the three
recolor passes - it was the HEIGHT BREAKPOINTS inherited unmodified
from stock Magic Sand.** User's screenshot showed almost the entire
visible sandbox stuck in a narrow teal-to-green band, with only a weak
yellow/orange smudge at the very peak - no deep navy, no crisp orange,
no cream, no white, on any of the three recolor attempts despite three
completely different color choices. Same recurring bug CLASS as the
`landT`/`waterRamp()` renormalization fixes on the OTHER branch
(`ss/ecosim-model-visuals-kiqw12`, 2026-09-28/10-16): a color ramp's
declared elevation range being far wider than what a participant can
physically reach makes most of the ramp's dynamic range permanently
unreachable, no matter which colors sit at which breakpoint.

Confirmed via direct code read, not assumption:
`SandSurfaceRenderer.cpp` (`setup()`, ~line 94-99) sets `elevationMin`/
`elevationMax` directly from `heightMap.getScalarRangeMin()`/
`getScalarRangeMax()` - i.e. straight from `HeightColorMap.xml`'s own
first/last key heights, NOT from any Kinect-calibrated range -
`heightMapScale`/`heightMapOffset` (and thus `depthfrag`'s lookup
position in the color table, computed in the vertex shader) are
derived entirely from this declared span. Every recolor pass so far
kept the stock file's inherited `-220mm` to `+220mm` declared range
(440mm total) unquestioned - but this box's REAL measured limits
(already established on the 2026-09-25 hardware-measurement session,
same physical hardware, see the hydrology-phase entries above) are
only **-41.8mm max dig / +80mm max mound** - under a third of the
declared span. So on every real mound/dig this box can produce, the
color lookup only ever lands in a narrow slice near the middle of the
table, which is exactly why only muted greens/teals ever showed up
regardless of which named color sat at which of the old `-220..220`
breakpoints - the vivid extremes (navy, orange, cream, white) were
mathematically unreachable by any real sculpting, the same failure
mode "barely noticeable" species gradients and "unreachable water
line" both turned out to be on the other branch, just never diagnosed
here until actually looking at a real screenshot.

**Fix: rescaled all 16 height breakpoints to this box's real physical
range** (`-45mm` to `+85mm`, a few mm of margin past the measured
`-41.8`/`+80` limits, same margin convention as the water-gradient
floor fix on the other branch) **- colors unchanged in content, only
WHERE each sits on the elevation axis.** The user's 7 named anchor
colors keep their exact hex values, now placed at representative
heights within the real range: Deep Water `-45mm` (the floor), Shallow
Water `-3mm`, Lowland Grass `0mm` (kept immediately adjacent to Shallow
Water - only 3mm apart - deliberately, to preserve the user's own
stated intent that this pair act as "a stark, high-contrast border"),
Foothill Grass `25mm`, High Highlands `55mm`, Mountain Peaks `78mm`
(at the real mound limit), Snow Caps `85mm` (a few mm past it, so
snow needs near-maximal mounding to reach, same "a few mm margin, not
the absolute limit" lesson as the water-gradient floor tuning
history). The other 9 keys are linear-RGB interpolations between
whichever two of these 7 anchors they fall between, same mechanical
technique as every prior pass - just applied to a realistic axis
instead of the stock file's inherited one.

**This is the first HeightColorMap.xml change in today's whole
sequence that's a structural fix rather than a fourth color
guess** - the first three passes could not have looked right no matter
which colors were chosen, since the real sand never reached most of
the table regardless. Still not independently re-run through a
colorblind simulator (same caveat as every prior pass), and **the
`-41.8`/`+80` figures being reused here are the SAME measurements
already flagged throughout this file as specific to this box's
calibration at the time they were taken** - if calibration or sand
depth has changed since, these breakpoints need the same re-measure-
and-rescale treatment, not just new colors. Not yet tested on real
hardware - this fix is reasoned from the box's own documented prior
measurements and a direct read of the transformation code, not a new
hardware session.

## Branch direction clarified, and water-flow particle visualization added (2026-10-19)

Same visit. User clarified the actual intent behind the
`ss/terrain-reference-visuals` split, which this file had been treating
as a side investigation off the "real" branch: **"I asked you to split
off because I felt it would be best to simplify the visuals, then build
things step by step and eventually add the ELF visuals back. I want to
stay on this branch so that we have a fallback with the previous one."**
So this branch is now the primary forward development line - rebuild
up from a clean, reference-matched bare-terrain baseline, re-adding ELF
visuals deliberately later - and `ss/ecosim-model-visuals-kiqw12` is the
fallback/reference copy, not the other way around as earlier entries in
this file implied. Noted here so this doesn't get re-litigated later.

**First piece built under that direction: a real-data particle/streak
visualization of water flow**, closing a genuine gap flagged repeatedly
in this file - `stepFlow()` (the hydrology phase's second slice, see
above) has been computing real per-cell water transport since early
October, but nothing has ever made that motion visible; water's color
alone can't show flow, only depth. This is also literally the third
piece of the Liu/ARsandbox014 succession-model methodology ("Future
phase" section above) finally being built - vector-field/particle-flow
imagery of water flow - deliberately NOT attempted back on 2026-10-16
(re: `anvaka/fieldplay`) because at that time it would have been
"the literal 'non-simulated decorative flow particles' option" already
passed over in the Strategic Reset - there was no real flow simulation
yet to drive it honestly. `stepFlow()` existing now removes that
objection: every particle's motion comes directly from real simulated
transport, not a second, separately-invented flow model.

**New `WaterFlowParticles.h`/`.cpp`** (`src/Games/`), modeled directly
on `CCritterController`'s shape (`setup()`/`setProjectorRes()`/
`setKinectROI()`/`update()`/`drawMainWindow()`/`drawProjectorWindow()`/
`drawGui()`, same FBO-compositing pattern) rather than inventing a new
convention - kept as its own class, same precedent as `EcologyParams`,
since it only ever *reads* `VegetationField`'s state and can be added/
removed/retuned with zero risk to the actual simulation.

**Two new read-only accessors added to `VegetationField`** (`.h`/`.cpp`),
deliberately NOT touching `stepFlow()` itself: `getWaterDepthAt(gx,gy)`
(just `waterDepth`'s own stored value) and `getFlowVelocityAt(gx,gy)` -
a 2D grid-cells/second vector summed from the exact same surface-height
(terrain+depth) difference to each of a cell's 4 neighbors that
`stepFlow()`'s own pass 1 already computes, just vector-summed instead
of kept as 4 separate scalars, and deliberately NOT scaled by
`stepFlow()`'s own per-cell outflow-limiting factor (that factor exists
to conserve depth correctly when actually moving water; a query reading
direction/relative magnitude for display has no such conservation
requirement, and skipping it means this can't affect, and isn't
affected by, `stepFlow()`'s own pass ordering). Real simulated physics,
queried rather than applied - not a parallel approximation.

**Particle mechanics**: each particle tracks a continuous (sub-cell)
grid position; every frame, a particle whose current cell's depth is
below `MIN_VISIBLE_WATER_DEPTH_FRAC` (the SAME constant `stepFlow()`'s
own hysteresis already uses - deliberately reused rather than a second
threshold) gets respawned at a random wet cell (bounded 20-try search;
a particle that can't find one this frame parks off-grid and keeps
retrying every later frame, so a bone-dry sandbox before any water
exists just means no particles render yet, not a hang). A live particle
is advected by `getFlowVelocityAt()`'s vector, scaled by a new
`PARTICLE_SPEED_SCALE` - a VISUALIZATION-ONLY multiplier, since real
single-cell-to-single-cell surface-height differences can be tiny
(much smaller than between cells several apart), separate from
`FLOW_RATE` itself. Drawn as a short streak (previous position to
current position), not a static dot, into an FBO exactly like
`CCritterController`'s own compositing.

**Cost, checked before writing anything** (this project's own standing
rule after the CA spread layer's regression): a few hundred particles,
each one depth read + one 4-neighbor velocity query per frame, no trig/
random beyond occasional respawns - orders of magnitude cheaper than
the ~150K-cell per-species passes that actually caused that regression.
`ENABLE_FLOW_PARTICLES` is still a real A/B kill switch (default on),
and `PARTICLE_COUNT` is live-tunable (default 400, GUI range 0-2000),
same defensive posture as every other new mechanism in this file.

**GUI**: new "Water Flow" ImGui panel (`WaterFlowParticles::drawGui()`)
with the enable toggle, particle count/speed/thickness/color controls,
and a live ROI-center depth + flow-velocity readout - same "measure
before tuning" posture as every other panel in this project, so
`PARTICLE_SPEED_SCALE` (first-guess, untested) can be set from a real
number instead of by feel once this is actually on hardware.

**Wired into `ofApp.h`/`.cpp`** following `critterController`'s exact
pattern: `setup()`/`setProjectorRes()`/initial `setKinectROI()` at
startup, re-`setKinectROI()` in the same block `critterController`'s
is in, `update()` after `vegetationField.update()`, and drawn in both
windows between `sandSurfaceRenderer` and `critterController` (so
streaks sit under agent icons, over the terrain/water color) plus the
new GUI panel. **Added to both `Magic-Sand-Ecosystem.vcxproj` and
`.vcxproj.filters`**, matching `EcologyParams.cpp`/`.h`'s existing
entries exactly - same standing reminder as when `EcologyParams` was
added: this project's Visual Studio build lists source files
explicitly, so a new file invisible to the `.vcxproj` would silently
not compile at all.

**Not yet tested on real hardware** - no openFrameworks SDK available
in this session (same constraint as `EcologyParams`), so this hasn't
been built. `PARTICLE_SPEED_SCALE=2000.0f` is a first guess with no
real-hardware velocity numbers to check it against yet; the GUI's live
readout is there specifically to make that the first thing checked
once this is pulled, before assuming the slider's default is remotely
close.

**FOUND AND FIXED same day, once this was actually built and run:
"tiny blinking blips that eventually disappear."** Two real, distinct
bugs, not one:

1. **No hysteresis on the respawn/liveness check.** The first version
   compared `getWaterDepthAt()` against `MIN_VISIBLE_WATER_DEPTH_FRAC`
   directly - a raw instantaneous threshold, exactly the same class of
   bug `WATER_HYSTERESIS_FRAC`/`waterState` already exists to fix
   elsewhere in this file (see the hydrology-phase pooling entries
   above: "it's flickering around the waterline," fixed once already
   for rendering and for Critter/HumanAgent movement). Using a second,
   separate raw check here reopened that exact bug on a third path.
   Fix: both checks now call `vegetationField->isWaterAt(kx, ky)` -
   the SAME hysteresis-stabilized `waterState` Critter/HumanAgent
   movement and the renderer already read - instead of a fresh raw
   comparison. "Read the one shared, already-stabilized state" is
   literally the lesson `isWaterAt()`'s own header note already
   recorded from the first time this bug was fixed; this was a case of
   not applying it to new code that needed the same property.
2. **Independent random-sample respawn search degrades badly when the
   wet fraction of the grid is small - confirmed, not just suspected,
   by matching the actual reported symptom.** The original
   `respawnParticle()` tried 20 independent random cells per attempt;
   each try is a blind coin flip with no memory of where water actually
   is, so once genuinely-wet cells are a small fraction of the ~153K-
   cell grid, success probability per attempt collapses and more and
   more particles fail every attempt and sit permanently parked off-
   grid (invisible) - directly matching "eventually disappear" over
   time, rather than a one-off glitch. Fix: a bounded LINEAR scan
   (up to 6000 cells, wrapping from a random start) instead of
   independent samples - a real body of water is spatially contiguous,
   so a scan crossing through even a small arc of the grid's row-major
   order reliably passes through it, unlike independent random
   sampling which has no such locality benefit.

**Separately, a real cosmetic bug, not a water-matching issue at
all:** a near-stationary particle (correct, physically expected
behavior in flat/undisturbed water with no real surface-height
difference to drive flow) draws as a zero- or near-zero-length line
(`ofDrawLine(prevPos, prevPos)`), which is exactly the "blinking" the
user reported - a degenerate line is an edge case some GL line-
rendering paths don't render consistently frame to frame. Fix: every
visible particle now also draws a small filled circle at its current
position (`ofDrawCircle`, radius derived from the existing
`PARTICLE_THICKNESS` slider) - a stable, always-visible mark regardless
of how much the particle moved that frame, with the streak line drawn
on top showing real motion whenever there's genuine motion to show.

**Important interpretive note, not yet confirmed either way:** the
screenshot that surfaced this (a lake with only a couple of visible
blips, mostly in the deepest part) may ALSO reflect a genuine mismatch
between what LOOKS like water (`HeightColorMap.xml`'s elevation-only
blue stops, rescaled just before this to this box's physical dig/mound
range) and what `VegetationField` itself classifies as water
(`waterLevelFrac`, derived from `TEMPERATURE`/`LIVING_RANGE_FRACTION`
against `VegetationField`'s OWN calibrated range - a completely
separate scale, same duplication this file already flagged once before
in the 2026-09-24 "Separately identified, not a bug" entry, there
between `SandSurfaceRenderer`'s cosmetic `elevationNorm` and
`VegetationField`'s classification). If so, large visible "blue" areas
on this branch may simply not be real water by the simulation's own
definition at all, regardless of how robust the particle system's own
respawn/liveness logic is - worth checking directly on real hardware
(compare the Water Flow panel's ROI-center depth readout against
whether that same spot visually looks wet) once this build is actually
running, rather than assumed fixed by the two bugs above alone.

Still not tested on real hardware - same constraint as the rest of
this entry.

## WaterFlowParticles replaced with WaterFlowField - wrong visualization paradigm, not a tuning problem (2026-10-19)

Same visit, after the two bug fixes above. User's own description of
the result: "What I'm seeing is something similar to the original
critters we created - a bunch of points that respond to gravity
(somewhat)." Pointed at Liu's work again plus a second reference (a
Google Scholar citation) as the actual target. Direct fetch of that
citation failed - `scholar.google.com` doesn't resolve from this
session's network (confirmed via `WebFetch`, same class of outbound
restriction this project already hit once before with
jayvandyke.com/makezine.com - a general proxy restriction, not
domain-specific). Rather than guess from a blocked link, asked the
user directly via `AskUserQuestion`, grounding the options in Liu's own
paper's literal wording (already read in full and quoted elsewhere in
this file): water flow "visualized in Grasshopper as flow-direction/
velocity/vorticity vector fields." **User confirmed: dense vector-field
glyphs** - small arrows at a regular grid over the water, each showing
local direction/speed, redrawn fresh every frame - not the discrete
tracked-and-advected particles this file's prior two entries had just
finished debugging.

**This explains the user's "critters" comparison precisely, and
retroactively reframes the last two bug-fix entries**: discrete dots
that persist, move, and get replaced were always going to read as
little creatures/agents, structurally - matching `Critter`'s own
sparse-icon rendering convention this project already uses for deer/
humans - no matter how well the flicker/respawn-starvation bugs got
fixed. Those two fixes were still real, correct bug fixes in their own
right (the hysteresis and respawn-search issues were genuine, confirmed
bugs, not artifacts of the wrong paradigm) - but they could never have
produced "vector-field imagery" by polishing a particle system, since a
sparse set of discrete tracked points is a different visualization
technique from a dense field of per-location direction/magnitude
glyphs, not a degraded version of the same one.

**Replaced the whole mechanism rather than retrofit it**: `WaterFlowParticles.h`/
`.cpp` -> `WaterFlowField.h`/`.cpp` (renamed via `git mv`, not left
under a now-inaccurate name), and every particle/respawn/advection
concept (the `Particle` struct, `respawnParticle()`, the bounded linear
scan, `PARTICLE_SPEED_SCALE`) is gone - replaced with a stateless
per-frame sample grid. `update()` now just loops over sample points
spaced `GLYPH_SPACING_CELLS` apart (default 8), skips any point
`isWaterAt()` doesn't currently call water (same hysteresis-stabilized
check the particle version's bug fix had just adopted - kept, since
it's the right check regardless of visualization style), reads
`VegetationField::getFlowVelocityAt()` fresh at that point, skips it
if the magnitude is below `MIN_VELOCITY_TO_SHOW` (calm water
legitimately has nothing to show, not a bug), and otherwise draws a
standard two-stroke arrow glyph (shaft + angled arrowhead, both
computed in grid space and converted to projector space only at the
final draw call, so any projective warping from calibration is handled
correctly for free - same technique the particle version's streak
lines already used). No persistent state, no respawn logic, no
liveness tracking needed at all - every glyph is independently
recomputed from `VegetationField`'s current data each frame.

**Cost re-checked for the new access pattern, not just assumed carried
over**: a dense per-Kinect-pixel sample grid would be tens of thousands
of arrows (both visually unreadable and a real per-frame cost); sampling
every `GLYPH_SPACING_CELLS` cells instead keeps it under ~2500 sample
points on this project's typical ~519x295 grid, each a cheap
`isWaterAt()` check plus the unchanged 4-neighbor `getFlowVelocityAt()`
query - same cheap operation class (no trig/random at volume) the
particle version's own cost budget already confirmed safe, just
re-verified for a different access pattern (a fixed sample grid instead
of N tracked particles) rather than assumed to carry over unexamined.

**`getWaterDepthAt()`/`getFlowVelocityAt()` (the two `VegetationField`
accessors added for the particle version) are unchanged and reused
as-is** - they were always genuine, paradigm-independent queries of
real simulated state, not particle-specific plumbing, so nothing
needed to change on the `VegetationField` side of this rename. Build
wiring (`ofApp.h`/`.cpp`'s `waterFlowParticles` member and all call
sites, both `.vcxproj`/`.vcxproj.filters` entries) updated to match the
new name throughout - no file left referencing the old name except
historical comments/this changelog.

**Still not tested on real hardware** - same constraint as every entry
in this file today. `GLYPH_SPACING_CELLS=8`, `MIN_VELOCITY_TO_SHOW=
0.0005`, and `ARROW_LENGTH_SCALE=200` are all first-guess defaults with
no real velocity numbers behind them yet (the same uncertainty flag
`PARTICLE_SPEED_SCALE` carried before this rename) - the Water Flow
panel's live ROI-center velocity readout is there specifically so these
can be set from a real number instead of by feel once this is actually
built and run.

**CONFIRMED same day, once actually built and run: real root cause
found via the live readouts, not another guess.** User's screenshot of
`WaterFlowField` showed a dense, perfectly uniform lattice of arrows
covering the ENTIRE image - including the visibly dry, high-elevation
orange patch, with zero visible difference in density between that and
the lake. A real vector field should show spatial variation (dense
near real flow, sparse/absent over calm/dry land); uniform coverage
with no pattern at all is itself diagnostic, not a tuning question -
asked the user to check two specific existing readouts rather than
guess a third time in one session.

**Readouts confirmed two separate, real things, not one:**
1. **Still-broken elevation range bounds** - Vegetation panel showed
   bound A/B still at the arbitrary `-300.000`/`300.000`, the exact
   same recurring miscalibration bug flagged repeatedly throughout
   this file (most recently on the other branch, 2026-09-22/25). Not
   a code fix - "Reset range to ±calibrated ceiling" in the Vegetation
   panel, same as every prior occurrence.
2. **The real bug: `stepFlow()` has no noise floor on its own
   per-neighbor diff, unlike the activity/temperature computation
   which already has one.** At the ROI center - a cell the Vegetation
   panel's own classifier called `water=no`, elevation 27.9mm, nowhere
   near the (miscalibrated but still internally consistent) -73.9mm
   water line - the Water Flow panel reported depth `0.0038` and
   velocity magnitude `~0.0034`: both small but genuinely nonzero,
   both sitting right at/near their respective visibility thresholds
   (`MIN_VISIBLE_WATER_DEPTH_FRAC=0.004`, `MIN_VELOCITY_TO_SHOW=
   0.0005`). That's the signature of accumulated sensor noise, not
   real standing water or real flow - a dry, undisturbed cell has no
   principled reason to show ANY nonzero depth/velocity at all.
   Confirmed directly: `stepFlow()`'s pass 1/pass 2 (and
   `getFlowVelocityAt()`'s own query, added for the particle version
   and reused here unchanged) compute `diff = surface - neighborSurface`
   from raw per-cell elevation readings with no threshold at all -
   unlike `ACTIVITY_NOISE_FLOOR`, which exists for exactly this reason
   (Kinect depth sensor noise, ~1-2mm per cell, present on literally
   every cell every frame even when nothing is touching the sand) but
   only ever applied to the SEPARATE activity/temperature computation,
   never consulted by `stepFlow()`. Since Kinect noise is present on
   every cell independently and roughly uniformly, letting it drive
   real (if tiny) flow everywhere produces exactly the reported
   symptom: a small, pervasive, near-threshold depth/velocity signal
   with no real spatial structure, dense and uniform across the whole
   grid regardless of actual terrain shape - precisely the "arrows
   everywhere, no pattern" screenshot.

**Fix: new `FLOW_NOISE_FLOOR_MM`** (`VegetationField.h`/`.cpp`, default
`3.0f` - the same physical noise magnitude `ACTIVITY_NOISE_FLOOR`
already uses, kept as a separate tunable since it gates a
conceptually different comparison: a pairwise difference between two
neighboring cells' noisy readings, not one cell's own frame-to-frame
delta). Converted to a fraction of the current calibrated range once
per `stepFlow()`/`getFlowVelocityAt()` call (`FLOW_NOISE_FLOOR_MM /
(elevationMax - elevationMin)`, cheap - calibration only changes on a
real recalibration, not frame to frame) and used to gate BOTH of
`stepFlow()`'s passes and `getFlowVelocityAt()`'s own diff sum: a
difference at or below the floor contributes nothing at all, same
"dropped entirely, not merely damped" convention `ACTIVITY_NOISE_FLOOR`
already established. The flat-sea boundary condition (`stepFlow()`'s
own unconditional depth clamp for any cell below `waterLevelFrac`) is
completely unaffected - this only changes what counts as real
cell-to-cell FLOW, not whether the sea itself has water.

**Expected effect, not yet re-verified on hardware**: genuinely flat/
undisturbed land, away from any real slope/dig/mound/shoreline, should
now settle to exactly `depth=0`/`velocity=(0,0)` instead of a small
noise-driven residue hovering near the visibility thresholds - so the
vector field should show arrows only where real flow actually exists
(the confirmed water body, real slopes/dug bowls), not a uniform
lattice across dry high ground. GUI slider added in the Hydrology
section right after "Flow rate," with inline text explaining the
2026-10-19 finding that motivated it. Also fixed the exact same
duplicated-check in `getFlowVelocityAt()` - it had re-derived its own
copy of `stepFlow()`'s diff logic for the particle/vector-field
visualization (deliberately, to avoid touching `stepFlow()`'s own
state) and would otherwise have silently kept showing noise-driven
arrows even after `stepFlow()` itself was fixed.

Not yet re-tested on real hardware - same constraint as the rest of
today's work, but this fix is traced directly to two real numbers from
an actual running build, not a code-review guess alone.

## FOUND AND FIXED the real, complete root cause (2026-10-20) - the noise floor alone was not enough

User rebuilt with the noise-floor fix and reported directly: "THIS IS
NOT WORKING," with a screenshot still showing a handful of stray arrows
scattered on clearly dry, high-elevation terrain (including right on
the orange peak), while the real water body's shoreline showed nothing
at all - far fewer arrows than before the noise floor, but still
visibly wrong, not fixed.

**Traced precisely by re-reading `stepFlow()`'s own pass 1/pass 2
interaction line by line, not guessed:** a completely dry cell
(`depth<=0`) in pass 1 was given `scale[idx] = 1.0f` - intended to mean
"nothing to limit, since this cell has no outflow of its own to cap,"
but `scale[idx]` is ALSO what pass 2 reads as `scale[nidx]` when a
NEIGHBOR computes how much it can pull FROM this cell. A scale of
`1.0f` told every neighbor "this cell can fully supply whatever the
raw elevation-difference math implies," regardless of whether it
actually held any water - so a real, ordinary terrain SLOPE between
two completely dry cells (not sensor noise, genuine elevation
variation, present almost everywhere a sandbox gets sculpted) was
enough on its own to manufacture real `waterDepth` out of nothing,
moving phantom water from high dry ground to low dry ground with zero
actual water ever involved. `getFlowVelocityAt()` (added for the
particle version, reused unchanged for the vector field) had the exact
same structural gap: it computed outflow direction from the queried
cell's own elevation difference to its neighbors without ever checking
whether the queried cell held any real depth at all.

**This also fully explains why yesterday's noise floor visibly helped
but didn't finish the job**: `FLOW_NOISE_FLOOR_MM` correctly filters
small, jitter-level differences, so the dense, uniform, covers-
everything lattice from the first screenshot collapsed to just a few
marks. But this second bug doesn't care about magnitude being small -
it fires on any GENUINE slope exceeding the floor, dry or not, which
is exactly why the remaining stray arrows clustered near real
elevation features (the orange peak) rather than being randomly
scattered like pure sensor noise would be.

**Fix, in both places this logic independently exists:**
- `stepFlow()`'s pass 1: dry cells now set `scale[idx] = 0.0f`, not
  `1.0f` - "this cell can send nothing," the literal truth, not
  "nothing needs limiting." Fixes both directions at once: a dry
  cell's own outflow (pass 2's `scale[idx]` read) and a dry cell's
  ability to be drained BY a neighbor (pass 2's `scale[nidx]` read)
  both now correctly zero out.
- `getFlowVelocityAt()`: now returns `(0,0)` immediately if the
  queried cell's own `waterDepth<=0`, before computing anything from
  neighbor elevations at all - a dry cell has no water to have a
  velocity, full stop, regardless of how steep the ground under it is.

**Both fixes are purely about gating on REAL EXISTING DEPTH, not a
second noise threshold** - `FLOW_NOISE_FLOOR_MM` and this fix address
two genuinely different failure modes (small-magnitude sensor jitter
vs. large-magnitude but illegitimate dry-to-dry transport) and both
are needed; neither alone would have fully closed the gap. The flat-
sea boundary condition is unaffected either way - it still applies its
own unconditional depth clamp after the flow step, regardless of this
cell's pre-flow `scale`.

Not yet re-tested on real hardware - same constraint as the rest of
this multi-day thread, but this fix is traced to an exact, confirmed
logic error (reading a wrong-purpose "nothing to limit" value as "able
to fully supply"), not another guess from a screenshot alone.
## WaterFlowField rebuilt as a terrain-gradient particle-trail system (2026-10-20)

User's hardware screenshot of the arrow version: a handful of white
ticks blinking in one corner, nothing over the visibly blue lake. Two
root causes, both structural rather than tuning:
1. Every glyph was gated per frame on `isWaterAt()` AND on
   `getFlowVelocityAt()` clearing `MIN_VELOCITY_TO_SHOW`. `stepFlow()`'s
   water settles flat at rest, so real velocity is ~0 nearly everywhere;
   what remains near the threshold is sensor noise, so glyphs flip on/off
   every frame.
2. `isWaterAt()` is VegetationField's water classification, which on
   this branch uses a different elevation scale from the
   `HeightColorMap.xml` blue the user actually sees (already flagged in
   the 2026-10-19 entries), so arrows and visible water don't coincide.

**Replacement (same class name, so no build-file changes):** the
convention the reference systems actually use (SND, Liu's flow imagery,
fieldplay-style flow maps) - many particles advected through a smooth,
persistent field, each leaving a fading trail and fading in/out over a
finite lifespan, so nothing ever pops. The field is the terrain's own
downhill gradient (gravity-driven runoff), computed from a new read-only
`VegetationField::getElevationGrid()` (this frame's existing samples,
no extra Kinect reads), downsampled (`FIELD_DOWNSAMPLE`=4), Gaussian-
blurred (`FIELD_BLUR_SIGMA`=1.5) and exponentially smoothed in time
(`FIELD_SMOOTHING_SEC`=0.5) before a Sobel derivative - that smoothing
is what keeps noise from making the flow jitter. Particles rain
uniformly, accelerate downhill (`GRAVITY`=30 cells/s^2 per mm/cell of
slope, `FRICTION`=2/s, `MAX_SPEED`=60 cells/s), collect and dim in
basins (`STILL_BRIGHTNESS`), and respawn after 2-6s. Trails are one VBO
line mesh drawn directly in projector space (the FBO was dropped: an
alpha-blended FBO over a transparent clear squares alpha and washes
faint trails out). Deliberately independent of any water-classification
threshold, so it cannot inherit the HeightColorMap/VegetationField scale
mismatch. Purely visual, no effect on the simulation.

`stepFlow()`/`getFlowVelocityAt()` are untouched and no longer read by
this class. Not compiled or hardware-tested here (no openFrameworks SDK
in this session); all defaults are first guesses, and the Water Flow
panel shows ROI-center slope and implied terminal speed so
GRAVITY/FRICTION can be set from real numbers.

## Particles masked to visible water; hydrology/seed-dispersal design reviewed (2026-10-20)

**Change:** `WaterFlowField` particles now exist only on visibly blue
water. New `VISIBLE_WATER_LINE_MM` (default -1.5mm, live slider):
spawning picks from a per-frame list of smoothed field samples below
it (always succeeds if any water exists), and a particle whose next
step would cross onto land stops at the shore and fades out. Scale
check done before choosing the value: `SandSurfaceRenderer` negates
the colour-map key heights and the vertex shader uses `+dot(basePlaneEq)`
where `elevationAtKinectCoord()` uses `-dot`, so the two negations
cancel - a `HeightColorMap.xml` key at height h renders exactly where
`elevationAtKinectCoord()` returns h mm. The teal key at -3mm meets the
green key at 0mm, hence -1.5mm. This is an interim mask that follows the
COLOUR MAP, not `stepFlow()`'s simulated depth.

**Known limitation, stated to the user:** inside standing water the bed
gradient only points to the deepest spot, so particles drift to the
bottom of each basin; there is no current, because nothing in the model
supplies one.

**Sources reviewed for the hydrology/seed-dispersal layer:**
- `openearth/sandbox-fm` (cloned and read): couples the Kinect sandbox
  to Deltares' Delft3D Flexible Mesh, a real 2D shallow-water solver.
  Sand height is written into the model bed (`zk`) each frame; the
  model's own boundary conditions (river discharge/tide, defined in
  model schematizations hosted on Deltares SVN, not in the repo) drive
  the flow. Particle display (`plots_cv2.py` `update_lic`/`seed_lic`):
  dots are seeded only where water depth >= 0.5, advected by the model's
  u/v velocity via `cv2.remap`, and erased wherever depth < 0.1. So
  "particles only on water" and "particles follow solver velocity" are
  both literally what it does.
- Dewitz et al., "AR Sound Sandbox" (INTETAIN 2017, Springer LNICST,
  DOI 10.1007/978-3-319-73062-2_5): the abstract confirms a Lattice
  Boltzmann-based particle simulation running through the deformable
  landscape. Full text was NOT readable from this session (Springer,
  EUDL, mirevi.de, opus4.kobv.de all fail DNS through the proxy), so
  the user's "particles all flow one way through participant-made
  channels" reading of the figures could not be checked against the
  text. It is consistent with a standard LBM channel setup (inlet on
  one side, outlet on the other, sand above a threshold as bounce-back
  walls), but that is an inference, not a confirmed fact.
- Liu (2020), per the earlier full read recorded above: a 2D Lattice
  Boltzmann model in Processing, shown as flow vector fields, plus
  agent-based seed dispersal driven by the hydraulic sim. No equations.

**Core finding for design:** one-way flow through channels is a
boundary-condition property (a source and a sink), not a terrain
property. `stepFlow()` currently has only the flat sea as a source and
no sink, so at rest it is static by construction - which is why every
flow visualization on it so far has shown noise or nothing.

**Recommendation put to the user, not yet decided:** keep the
height-field (virtual-pipe) shallow-water approach rather than switch to
LBM - it uses continuous sand height and real depth (LBM treats sand as
binary walls), and it is what sandbox-fm's solver class approximates.
Upgrade `stepFlow()` to the full Mei et al. (2007) pipe model with
persistent per-edge flux (gives inertia and a real velocity =
flux / depth), add an inflow source and an outflow sink, render water
from simulated depth instead of the colour map, and advect particles
by that velocity, masked by depth. Seeds then ride the particles
(hydrochory) and settle where velocity drops or at the shoreline,
handing off to the Phase 3 suitability test. Open decisions for the
user: where the source/sink sit (fixed opposite edges vs. operator-
placed), whether the flat sea stays, and whether visible water should
switch from the colour map to simulated depth on this branch.

Not compiled or hardware-tested (no openFrameworks SDK in this session).

## Hydrology step 1: virtual-pipe model with source and sink (2026-10-20)

User deferred the three design questions to Claude's recommendation and
approved step 1. Decisions taken: fixed source on one edge and open sink
on the opposite edge (left -> right by default, both selectable), flat
ELF sea removed as a water source (kept as an off-by-default legacy
toggle, `USE_FLAT_SEA`), visible water to come from simulated depth in
step 2.

**`stepFlow()` rewritten as the full Mei, Decaudin & Hu (2007) virtual-
pipe model.** Persistent per-edge outflow fluxes (`fluxL/R/T/B`) give the
water momentum; the old scheme moved water proportional to the surface
difference and could only settle, so at rest nothing moved. Per substep:
source inflow, flux update (gravity x surface difference, exponential
friction, clamp >= 0, K-factor scaling so a cell never sends more than
it holds - dry cells send nothing), then depth and velocity
(net flux / mean depth, zero below 0.5mm). The sink edge's outward pipe
sees an outside surface at the cell's own bed, so water drains off it;
the other edges are walls. Internals are in mm and grid cells (storage
stays fraction-of-range for the existing consumers), so flow speed no
longer depends on the calibrated range or the recurring ±300 bounds bug.
Terrain under the water is exponentially smoothed
(`FLOW_TERRAIN_SMOOTHING_SEC`=0.3) instead of the old `FLOW_NOISE_FLOOR_MM`
deadband, which is removed along with `FLOW_RATE`, `waterDepthNext`,
`flowScale`. New tunables: `FLOW_GRAVITY`=80, `FLOW_DAMPING_PER_SEC`=0.3,
`FLOW_SUBSTEPS`=2, `SOURCE_EDGE`/`SINK_EDGE`, `SOURCE_SPAN`=0.4,
`INFLOW_MM_PER_SEC`=8. New: `getFlowVelocityAt()` now returns the solver's
real velocity, `isWaterCell()`, `resetWater()`, GUI readouts (cost ms,
wet cells, volume, in/out rates, max depth, ROI-centre depth/velocity).

**Hysteresis bug fixed:** the band was MIN -/+ HYST with MIN 0.004 <
HYST 0.014, so the off-threshold was negative - a cell, once water,
could never dry. Hidden while the flat sea never drained. Now on above
MIN + HYST, off at MIN; HYST default lowered to 0.007 (~2mm).

**Pre-hardware check (NumPy mirror of the same scheme, scratchpad only,
not committed):** 260x150 grid, noisy terrain with a carved channel
dropping ~40mm across the table. Stable at 30 and 40 fps (no NaN, no
negative depth), volume conserved exactly, all wet-cell x-velocities
positive (one-way). With friction 1.5/s flow crawled (~1.5 cells/s mean,
terminal speed ~ G x slope / friction / depth); at 0.3/s it reaches true
steady state (volume flat ~10k, outflow -> inflow, water confined to the
channel, ~8 cells/s mean) - hence the 0.3 default. Velocity is bounded
by ~1/substep-dt at thin cells near the sink (K limiter), cosmetic only.

**Verification aid:** `WaterFlowField` gained "Particles live on"
(colour map / simulated water) and "Particles moved by" (bed slope /
simulated velocity), both defaulting to simulated, so the hydrology is
visible as particles before step 2 draws simulated water. Not compiled
or hardware-tested (no openFrameworks SDK in this session).

Next: step 2 (draw water from simulated depth over the colour map),
step 3 (polish particle advection), step 4 (hydrochorous seeds).

## Hydrology step 2 + particle visibility fix (2026-10-20)

User's hardware screenshot after step 1: particles appeared only as
faint white specks in the deep right-hand basin and the bottom-right
corner, "barely visible on the sand." Read as: the simulated water had
reached and was filling the basin (expected - a depression must fill to
its rim before spilling toward the sink), but the particles were
unreadable and the simulated water itself was not drawn at all.

**Particle visibility:** trails were `OF_PRIMITIVE_LINES` with
`ofSetLineWidth()`; on core-profile GL contexts widths above 1px are
silently ignored, and a particle in near-still water has a near-zero-
length trail, dimmed to `STILL_BRIGHTNESS`=0.25 - so pooled particles
were 1px, quarter-brightness dots. Now one `OF_PRIMITIVE_TRIANGLES`
mesh: each segment a quad `TRAIL_THICKNESS` (3px) wide, each particle a
`HEAD_SIZE` (4px) square head so still particles stay visible;
`STILL_BRIGHTNESS` 0.7; brightness now normalised by a separate
`FULL_BRIGHTNESS_SPEED` (10 cells/s) instead of the bed-slope mode's
`MAX_SPEED` (60), which made simulated-velocity particles (~8 cells/s)
permanently dim.

**Step 2, simulated water drawn:** `heightMapShader.frag` (GL2/GL3, in
sync) gained an overlay after the colour-map lookup: it samples the
vegetation texture `SandSurfaceRenderer` was already binding every frame
(`vegetationSampler`/`vegetationGridOrigin`/`vegetationGridStep`/
`hasVegetation`, plus `texcoordfrag` which the vertex shader still
outputs), decodes alpha 1..249 as depth (0 dry, 255 snow ignored), and
blends Shallow `#41B6C4` -> Deep `#08306B` (the user's own palette
stops) at 60-95% opacity by depth. New `VegetationField::
DRAW_SIMULATED_WATER` (default on, checkbox in the Hydrology section),
sent as the `drawSimWater` uniform. Contour lines still draw on top.
Only cells the hysteresis classifier calls water (~3mm+) are drawn.

**Known confound, raised with the user, not changed:** `HeightColorMap.
xml`'s keys below 0mm are blues, so dry low ground still LOOKS like
water under the colour map, competing with the real overlay. Recolouring
those keys to a dry tone is the obvious fix but is a palette decision
for the user. No GLSL validator available in this session (no network
for apt), so shaders were checked by hand only; not compiled or
hardware-tested.

**Follow-up, same day: filled-quad particles reverted at the user's
request ("I prefer the look of the old particles... make the tails
longer").** Back to `OF_PRIMITIVE_LINES` trails, `TRAIL_THICKNESS` 2,
no head squares (`HEAD_SIZE` removed). `TRAIL_LENGTH` default 20 -> 60
frames, `MAX_TRAIL` 48 -> 120 so the slider can go further. Kept from
the quad pass: `STILL_BRIGHTNESS` 0.7 and the `FULL_BRIGHTNESS_SPEED`
normalisation (alpha only, not shape). The simulated-water shader
overlay is unaffected. Caveat stated to the user: longer tails only help
moving particles - a particle in still water still has a near-zero-
length trail, and core-profile drivers may still clamp line width to 1px.

## Hybrid hydrology: lattice-Boltzmann flow inside the pipe model's water (2026-10-20)

User: the line-trail particles were "better, but not quite looking like
the hydrology model I'm expecting, or like the Dewitz particle system."
Asked (message truncated) why not use LBM like Dewitz/Liu. Answered
candidly: the earlier "LBM treats sand as binary walls" objection was a
tradeoff, not a disqualifier. The pipe model is gravity-driven runoff -
water fills basins and only a narrow spill path moves, so its steady
state is mostly still - while the references' look (a continuous,
obstacle-skirting, eddying one-way stream) is what a 2D LBM driven from
an inlet produces. Costs of pure LBM stated: no depth/wet-dry (weakens
the "moisture fed by genuine water flow physics" contribution claim),
threshold-wall flicker, stability tuning, December timeline. **User chose
the recommended hybrid:** pipe model keeps deciding WHERE water is
(wet/dry, depth, pooling, future moisture field); a coarse LBM decides
HOW it moves.

**New `LatticeFlow.h/.cpp`** (own class, added to both `.vcxproj` and
`.vcxproj.filters`), owned by `WaterFlowField`, read-only against
`VegetationField`. D2Q9 BGK on the grid downsampled by `DOWNSAMPLE`=4
(~9.6K cells); a lattice cell is fluid when >= `WET_FRACTION` (0.5) of
its fine cells are `isWaterCell()`; newly wet cells start at rest; dry
sand and grid edges are halfway bounce-back walls. Inlet: fluid cells on
`VegetationField::SOURCE_EDGE` within `SOURCE_SPAN`, set to equilibrium
at density 1 and inward `INLET_SPEED`. Outlet: fluid cells on
`SINK_EDGE` set to equilibrium at density 1 with the interior
neighbour's velocity. Cells with non-finite or out-of-range density
reset to rest (counted in the GUI). Velocity reported in grid cells/s
(lattice speed x DOWNSAMPLE x steps / frameDt), bilinear, zero on walls.

**Tested before committing** by compiling the real `LatticeFlow.cpp`
against small ofMain/VegetationField stubs (scratchpad, not committed)
on a 519x295 meandering channel with a side basin and an island:
- First version (copy-from-neighbour outlet): flow decayed 3.7 -> 0.6
  cells/s as pressure built inside and choked the inflow.
- Interior-density inlet + pressure outlet: positive feedback, lattice
  speed 0.62 and hundreds of cell resets.
- Final (density-1 inlet, density-1 pressure outlet): steady, 0 resets,
  ~0.3 ms/frame, flow follows the meander, recirculation in the basin
  and island wake. Inlet 0.12 / tau 0.55 (defaults): max lattice speed
  0.143, channel centre ~17-21 cells/s, 0 resets.

**`WaterFlowField` changes:** new `MOTION_LATTICE` mode (default) using
`LatticeFlow::velocityAt()`; `EMIT_AT_SOURCE` (default on) releases
particles from wet cells within `SOURCE_BAND_CELLS` (6) of the source
edge, falling back to anywhere on the water; `STALL_SPEED`/`STALL_SEC`
(1 cell/s for 1.5 s) retire particles stuck in dead water; lifespans
raised to 15-40 s so particles can cross the table. GUI: lattice
section with tunables and readouts (fluid/inlet cells, cost ms, max
lattice speed, resets). Avoided `near` as an identifier (a Windows
macro). WaterFlowField changes not compiled (no oF SDK); not
hardware-tested.

---

## Entries written directly in this log

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

## 2026-10-22 - Particles on simulated water only; dry-earth colour map below 0 mm

Decision (user): the definition of water is the flow simulation
(`VegetationField::isWaterCell`, the cells the simulated-water overlay
draws), not the colour map. Previously both painted blue and disagreed:
the colour map coloured every cell below 0 mm blue whether or not water
had reached it.

- `WaterFlowField`: new "Particles shown on" setting, Water only (default)
  / Everywhere. Water only passes a per-cell `isWaterCell` mask to
  `DewitzFlow::setWaterMask()` each frame; Everywhere passes nullptr and is
  the previous behaviour (harness numbers reproduced exactly).
- `DewitzFlow` particle handling with a mask (the lattice, obstacle mask,
  inlet and weights are untouched): particles spawn on non-obstacle water
  cells within `SPAWN_BAND_CELLS` (9 fine cells, ~3 lattice cells) of the
  inlet edge, or on any water cell if none touches the inlet, never on
  land; with no water anywhere they park invisibly and retry each frame.
  A particle that moves onto land keeps moving with the flow, fades out
  over `LAND_FADE_SEC` (0.5 s) and then respawns; the fade is committed,
  so re-entering water does not restore it (no shoreline flicker).
  Per-particle alpha is drawn as per-vertex colour.
- Scratch check (synthetic water band + separate pond, 2000 frames): no
  full-opacity particle on land, every respawn inside the inlet band on
  water, and on a dry table all 20000 particles park.
- `HeightColorMap.xml`: the six keys below 0 mm are now dry earth tones,
  light sand (226,205,165) at -3 mm to deep brown (104,74,46) at -45 mm.
  Keys at or above 0 mm unchanged; contour lines unchanged. The
  simulated-water overlay stays on by default, so blue means simulated
  water only.

## 2026-10-22 - Water table: seepage as a second source in stepFlow()

At the user's request. Pipe model, DewitzFlow, particles and colour map
unchanged.

- New `VegetationField` settings (Hydrology GUI): `ENABLE_WATER_TABLE`
  (on), `WATER_TABLE_MM` (-15 mm), `SEEPAGE_MM_PER_SEC` (4).
- `addSeepage()`, called each substep after the edge inflow: any cell
  whose surface (smoothed bed `flowTerrain` + depth) is below the table
  gains min(SEEPAGE x dt, table - surface). It is ordinary depth, so it
  is drawn, classified by `isWaterCell`, carries particles and flows on.
  Water above the table is moved only by the pipe model.
- `getDepthBelowWaterTableAt(gx, gy)`: smoothed bed minus the table, mm
  (positive above the table). Not drawn.
- GUI readouts: seepage inflow (mm x cells^2 / s), cells whose bed is
  below the table.

Synthetic test (the real `stepFlow`/`addSourceInflow`/`addSeepage` source
extracted and compiled against a stub class; 260x150 grid, 1.5 mm noise
per frame, 40 fps; scratchpad, not committed):
1. Basin with a -40 mm floor, inflow off: centre surface reaches the table
   in ~1.5 s, basin volume ~95% full by 5 s (seepage falls from ~4900/s to
   ~200/s). Afterwards the surface settles ~1-1.5 mm ABOVE the table and
   volume creeps up slowly (+6% over 5 min, slowing): Kinect noise in the
   bed briefly dips some cells' surface below the table, seepage tops them
   up, the pipe model spreads it, and nothing removes it again.
2. Shallow basin (floor -8 mm, above the table) on a gentle slope: no
   seepage and no water with inflow off; with edge inflow on, it becomes
   water when the surface flow arrives (~60 s on that slope), then holds
   water.
3. Basin filled, then the hole refilled with sand over 2 s: 10 s later
   0 water cells, but ~16,900 mm x cells^2 remains as an invisible film
   up to 2.9 mm deep (below the ~3-4 mm visibility threshold) on the new
   surface - the pipe model has no infiltration, so it does not vanish.
4. Flat sand at 0 mm with 1.5 mm noise, 60 s: never any water, 0 cells
   below the table. Harsher check, sand at -10 mm with 3 mm noise: never
   visible water (442 mm x cells^2 of sub-threshold seepage).

## 2026-10-28 - Succession model with site feedback (branch `ss/succession-feedback`)

**Task.** Replace V_t = f(h_t, T_t) (ELF bands, thesis Eq. 2.1) with
V_{t+1} = f(h_t, T_t, S_t), S_{t+1} = g(S_t, V_t) (Eq. 2.2), and connect
`EcologyParams` to it. Build freeze 2026-11-02. Step 0 (CLAUDE.md condensed,
history moved here) committed separately first.

**Decisions put to the user before coding.**
- "Keep the existing vegetation rendering" could not be done as written:
  this branch line draws no vegetation. User chose to port the species
  rendering from `ss/ecosim-model-visuals-kiqw12` (winner-take-all, emerald/
  orange-red/teal mixed toward white with height) with **opacity = total
  cover**. Height for the white mix is the colour map's normalised
  elevation (its keys span the reachable -45..85 mm), replacing that
  branch's `landT`, whose water/snow fractions use a different scale here.
  `DEBUG_HIDE_VEGETATION` default changed true -> false so it is visible.
- Disturbance: the user kept it as written (any change over
  `DISTURBANCE_MM` multiplies S by `SOIL_RETAINED_FRACTION`, digging or
  filling). Consequence: on the sandbox nothing produces new ground with
  S = 0 except the initial state or "Reset vegetation and soil"; T3's new
  fill gets S = 0 set directly in the harness.

**Built.**
- `SuccessionModel.h/.cpp` (plain C++): fields V (3 roles), M, S; inputs:
  elevation, `isWaterCell` mask, `getDepthBelowWaterTableAt`, mmPerCell, dt,
  temperature and its resting value, the spread radius and ELF's spontaneous
  chances. Model as specified (see the header note). Implementation choices:
  - Neighbour seeding re-implements `hasEstablishedNeighbor` inside the
    model (the original uses `ofRandom`/`cv::Mat`): same random disk
    samples within `SPREAD_RADIUS_MM`, from a fixed table of 512 offsets;
    a neighbour counts as established above `ESTABLISHED_COVER` = 0.1 (the
    ELF version used > 0, which in a continuous model is almost everywhere);
    re-checked on 10% of cells per step, the flag persisting in between.
  - Elevation: EMA (0.5 s) then a 7x7 box blur before the slope (degrees,
    via a lookup on the squared gradient).
  - Disturbance: after an event the cell's reference follows the sand for
    `DISTURBANCE_SETTLE_SEC` (1 s) and stays bare, so one dig is one event;
    reference time constant otherwise 30 s.
  - `UPDATE_INTERLEAVE` = 3: each step updates every 3rd row with 3 x dt
    (each cell at ~13 Hz). A full-grid step measured 4.5-4.9 ms median on
    the development container; interleaved, 2.2 ms.
  - Units: moisture fraction|percent; slope degrees|percent (slope);
    soil fraction|percent|cm|mm (cm/mm via `SOIL_MAX_DEPTH_CM` = 30), matched
    on the start of the unit string. Refused: unknown unit, min == max == 0,
    min > max.
- `EcologyParams::load()` refuses presets `SuccessionModel::validateParams`
  rejects, keeps the previous one, and shows the reason in the GUI.
- `VegetationField`: "Vegetation model" combo (ELF bands / Site feedback,
  default site feedback); the ELF block is skipped (densities kept) in
  site mode; model cover written to the texture's R/G/B (climax/pioneer/
  intermediate); new site texture (R = S, G = M); `eat*` remove the model's
  cover in the same preference order; GUI section "Succession" with all
  rates and readouts; "Reset vegetation and soil".
- Shaders (GL2/GL3, in sync): soil tint (brown x `SOIL_TINT_STRENGTH` x S,
  default 0.35, not on water), vegetation, water overlay, M/S false-colour
  debug overlay (off by default), contours.

**Designer defaults** (first guesses; met the timing targets without
retuning): growth 0.5/0.5/0.4 per s, mortality 0.3, soil build 0.01 with
weights 1.0/0.5/0.3, soil loss 0.005, margin 0.25, moisture falloff 16 mm,
soil retention 0.2, wet 2 s, dry 60 s.

**Harness** `tools/succession_harness/` (compiles the real
`SuccessionModel.cpp`; reads the real preset XML with a flat-tag reader;
160x100 grid at 1.9 mm/cell for T1-T5, 519x295 for T6-T7; 40 fps; flat
ground at 0 mm = 15 mm above the table; +/-1 mm noise unless stated).
Results:
- T1 PASS. Pioneers > 0.3 mean cover at 9 s, dominant 5-66 s, peak 43 s;
  intermediate dominant 66-227 s, peak 113 s; climax dominant from 227 s,
  peak 279 s. Final S 0.78, M 0.55.
- T2 PASS. With `SOIL_BUILD_RATE` = 0: pioneer 1.00, others 0 after 600 s.
- T3: patches raised 10 mm at 360 s (one disturbance event per cell, 3200
  cells). A keeps S 0.47; B set to S 0. Literal criterion (intermediate
  cover >= 0.5, A faster) FAILS: A never reaches 0.5 intermediate (max
  0.46) because at S 0.47 climax is also partly suitable and its
  neighbours seed it, so A goes straight to an intermediate/climax mix.
  Intermediate-or-later cover >= 0.5: A 7 s, B 71 s (B: pioneers, then
  intermediates at 71 s, climax ~210 s). Patch rims (the 10 mm step)
  stay bare: their slope is outside the ranges.
- T4: water on a dry strip (55 mm above the table) 10-40 s. M = 1 under
  water, halves 45 s after it stops. Nothing grows under water (M = 1 is
  outside every moisture range); cover > 0.1 from 57 s, pioneers then
  intermediates (soil passes 0.3), last >= 0.5 at 153 s, bare from 175 s.
- T5 PASS. Designer accepted; published refused ("pioneer moisture: min
  and max are both 0"); unknown_unit.xml refused ("intermediate slope: unit
  \"gradians\" is not recognised"); afterwards the model is identical to a
  fresh designer run; alt_soil.xml (intermediate soil 0.2-0.95, climax
  0.9-1.0) ends intermediate-dominant where designer ends climax.
- T6 PASS. 519x295, +/-2 mm uniform noise per frame, 600 s: 0 events.
- T7 PASS (interleaved). Median 2.21 ms, mean 2.28, 90th pct 2.55 (full
  grid: median 4.87 ms).

**Not done / not verified.** Not compiled with openFrameworks; shaders not
compiled (no GLSL validator in the session); nothing hardware-tested. The
projected role colours (emerald vs teal) are only dE 11.5 apart for normal
vision (dataviz validator) - noted, not changed (no palette iterations).

## 2026-10-28 - Colour map: new land colours, stepped bands

At the user's request (branch `ss/succession-feedback`). Water overlay,
DewitzFlow particles and contour lines unchanged; no code changed.

- `HeightColorMap.xml`: the 10 keys at or above 0 mm replaced by six
  colours in equal bands from 0 mm to the highest key (85 mm, 14.17 mm
  each): #B4BC92 0-14.17, #D6D27A -28.33, #F2D45C -42.5, #F5AE45 -56.67,
  #E07F1F -70.83, #B9561A -85. Earth-tone keys below 0 mm keep their
  heights and colours.
- Stepped, not blended: `ColorMap::updateColormap()` always interpolates
  linearly between keys, so each band is written as two keys with the same
  colour (its start, and 0.01 mm below the next band's start). This makes
  the whole map stepped, including the six earth-tone bands below 0 mm
  (each band now holds its key's colour up to the next key).
  Range is still -45..85 mm, so the shader's colour-map transform and the
  vegetation's height-to-white mix are unchanged.
- Check: the same interpolation re-run in Python over the 512-entry table
  gives exactly 12 runs of identical colour, one per band, with no
  intermediate colours (transitions within one 0.25 mm texel).
