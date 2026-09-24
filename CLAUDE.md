# Project notes for Claude

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
