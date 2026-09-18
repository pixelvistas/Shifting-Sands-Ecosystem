# Project notes for Claude

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

The slope-based movement energy cost from the paper is a plausible design
reference for the *future succession-model phase* (Xun Liu's paper), not
something to port now.

**Do not start implementing any of this until the ELF port itself is
confirmed working properly on real hardware** - that's the current
priority per the user's explicit instruction (2026-09-18).

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
- **Our port's growth is continuous instead, and NOT something the user
  asked for.** `VegetationField.cpp` (~line 206-208) does
  `density += GROWTH_RATE * dt` (real seconds) clamped to 1.0, with
  `SHRUB_GROWTH_RATE=0.2f`, `FRUIT_GROWTH_RATE=0.6f`, `NUT_GROWTH_RATE=0.4f`
  - a full regrowth in ~1.7-5 seconds of continuous in-band time, preserving
  ELF's 1:3:2 ratio but not its per-tick coin-flip pace. This was an
  unprompted call made by an earlier session at the very first vegetation
  commit (`86b416f`), whose own header comment says outright it was
  "re-expressed as continuous per-cell density rather than ELF's per-tick
  random growth, so it reads as smoothly thickening/thinning patches
  instead of tick-by-tick flicker" - a smoothness rationale invented and
  applied without ever being put to the user as a decision. A later pass
  (`055ae18`) re-derived the 1:3:2 ratio to match ELF's
  SHRUBGROWTH/FRUITGROWTH/NUTGROWTH more precisely but kept the continuous
  mechanic rather than reconsidering it. **Needs an explicit call from the
  user**: revert to ELF's literal per-tick probabilistic growth, or keep
  continuous growth now that it's flagged as a real (not rubber-stamped)
  fidelity departure.
- **RESOLVED - grazing pressure is not the cause.** User ran the proposed
  test (removed all deer/humans via the Population panel, waited) on a
  build before the white->black negative-space change. Result: still
  just blank white, no visible vegetation. With zero agents there is
  nothing left to strip cells bare, so growth itself is not happening at
  any rate - the "~120 agents outrunning regrowth" hypothesis is dead.
  Whatever's wrong is upstream of agents entirely.
- **Open, more fundamental question this reopens:** is elevation ever
  landing inside a growth band at all, or is the vegetation pipeline
  itself not engaging? Blank white is ambiguous in this shader - it's
  both the "nothing grown yet" base color AND the flat snow color when
  `debugShowSnow` is off - so "blank white" alone doesn't distinguish
  "no band ever satisfied" from "everything reads as snow" from "the
  overlay isn't wired up." Follow-ups asked of the user (2026-09-18,
  awaiting answer): (1) was there ANY variation at all - contour lines,
  water tint - or literally uniform flat white everywhere; (2) was the
  debug-snow toggle (`08a810a`) on during this test, and if so was the
  pale-blue snow tint ever visible; (3) what did the Vegetation panel's
  live readouts show (raw elevation at ROI center, water/snow line mm,
  calibrated ceiling, band widths) at the time; (4) which build/commit
  was this tested on. This is now a higher-priority thread than the
  growth-mechanic question above, since it may be the same root cause
  as the earlier "no height map at all" reports rather than anything
  vegetation-specific.
