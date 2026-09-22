# Project notes for Claude

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

**Not scoped or designed yet.** When this phase starts, first steps are
likely: (a) re-reading both source papers in full for the actual
fluvial simulation math/algorithm Liu used (not just visual style), (b)
deciding how a water-flow simulation should integrate with the existing
elevation-band water/snow classification already in `VegetationField`,
and (c) designing the seed-dispersal/competition model as a genuine
extension of the three existing plant types rather than a replacement.

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
