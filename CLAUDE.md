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
