# Ecological Simulation Module — Technical Specification

Shifting Sands · working draft, August 2026
Companion to `ECOSIM.md` (concept and positioning)

---

## 1. Terminology

Exact terms, so the same word means the same thing in the thesis, the code, and
the wall text.

### 1.1 Succession

| Term | Definition | In this system |
|---|---|---|
| **Primary succession** | Colonization of a substrate that has never supported a community and has no developed soil | The premise. Sand begins sterile and soil-free. |
| **Sere** | The complete successional sequence from bare substrate to a persistent community | One full run of the installation from a fresh surface |
| **Seral stage** | One identifiable community state within a sere | Corresponds to which species currently dominates a cell |
| **Pioneer species** | First colonizer; broad tolerance, high dispersal, low competitive ability | Species index 0. High `r_estab`, wide suitability curves, low `K_max`. |
| **Late-seral species** | Later colonizer; narrow tolerance, low dispersal, high competitive ability | Highest species index. Requires accumulated soil to pass its suitability threshold. |
| **Autogenic succession** | Change driven by the community's own modification of its environment | Pedogenesis. Vegetation builds soil, soil raises water-holding capacity, which admits later species. |
| **Allogenic succession** | Change driven by external forcing independent of the community | The participant's hands. Reshaping rewrites the heightfield and therefore the hydrology. |
| **Facilitation model** (Connell & Slatyer 1977) | Early species modify the environment to make it *more* suitable for later species and *less* for themselves | The mechanism the soil loop implements. Cite this directly. |
| **Nurse effect** | An established plant ameliorating microclimate for establishment nearby | Vegetation shading reduces particle death probability in its cell |

Autogenic and allogenic are the two terms that do the most work here: soil is the
autogenic driver, hands are the allogenic driver, and the interaction between
them is the piece nothing in the G-TUI literature has.

### 1.2 Niche and suitability

| Term | Definition | In this system |
|---|---|---|
| **Law of tolerance** (Shelford 1931) | A species persists between a minimum and maximum of each environmental factor, with an optimum between | The shape of every suitability curve |
| **Suitability Index (SI)** | A 0–1 score for one environmental variable against one species' response curve | `SI_moisture`, `SI_slope`, `SI_soil` |
| **Habitat Suitability Index (HSI)** | Composite of several SI values into one 0–1 habitability score | `S_s(c)` — combined suitability of cell `c` for species `s` |
| **Law of the minimum** (Liebig) | Performance is set by the scarcest limiting factor, not the average | Justifies `min()` aggregation; geometric mean is the softer default |
| **Fundamental niche** | Range a species could occupy given abiotic conditions alone | `S_s(c)` before competition |
| **Realized niche** | Range it actually occupies once competition is applied | `S_s(c)` after the shared carrying-capacity term |
| **Edaphic factor** | Soil-related environmental variable | `D(c)`, soil depth |

### 1.3 Hydrology and soil

| Term | Definition | In this system |
|---|---|---|
| **Overland flow** | Water moving across the surface rather than through it | What the particles are |
| **D8 flow routing** (O'Callaghan & Mark 1984) | Each cell drains to the single steepest of its eight neighbours | Basis of the gradient map, softened by jitter so channels braid |
| **Flow accumulation** | Upslope area draining through a cell | Emergent from particle residence, not computed separately |
| **Residence time** | Duration water remains in a cell | The quantity particles deposit; the definition of moisture here |
| **Field capacity** | Water a soil retains against gravity after drainage | `Wcap(c)`, raised by soil depth. The feedback variable. |
| **Water-holding capacity (WHC)** | Volume of water a unit of soil retains | `whc_per_depth`, the coefficient linking soil to field capacity |
| **Evapotranspiration (ET)** | Combined evaporation from surface and transpiration through plants | Modelled as per-particle death probability, not a bulk flux |
| **Pedogenesis** | Soil formation | Organic matter accumulation under persistent vegetation |
| **Propagule** | Any dispersing unit — seed, spore, fragment | What a tagged particle carries |
| **Propagule pressure** | Rate of propagule arrival at a site | `P(c,s)`, sum of neighbourhood dispersal and hydrochory deposits |
| **Hydrochory** | Seed dispersal by water | Particles tagging in vegetated cells and depositing downstream |
| **Allogenic disturbance** | Externally-caused destruction of standing community and substrate | Hands in the sand |

---

## 2. Time base

All rate parameters are expressed **per sim-year**, so published ecological
values can be entered directly from literature without conversion.

```
SIM_YEAR = the ecological time unit
dt_sim   = TICKS_PER_SIM_YEAR ⁻¹   // sim-years advanced per tick
```

`TICKS_PER_SIM_YEAR` is the single deferred constant. Pacing, compression, and
whether time advances continuously or in stepped events are all decisions about
this one number and are out of scope here. Set it to something plausible
(e.g. 120 ticks/sim-year at 60 fps = 2 s per sim-year) and move on.

Hydrology runs on wall-clock `dt`, not sim-years. Water is fast enough that
participants should see it respond in real time regardless of ecological pacing.

---

## 3. State representation

Simulation grid `G` matches the Kinect depth resolution (640 × 480) unless
profiling says otherwise. All fields are GPU textures; the CPU holds only
parameters and the puck list.

### 3.1 Field textures

| Handle | Format | Channels | Range | Written by | Read by |
|---|---|---|---|---|---|
| `texHeight` | R32F | `h` — elevation, mm | sensor range | KinectProjector | gradient, erosion |
| `texGradient` | RGBA32F | `∂h/∂x`, `∂h/∂y`, `‖∇h‖`, unused | — | `updateGradient()` | particles, vegetation, erosion |
| `texObstacle` | R8 | occlusion mask | 0–1 | `updateObstacle()` | particle advection |
| `texWater` | RG32F | `W` current water, `Wcap` field capacity | 0–1 | particle deposit, ET | vegetation, particle death |
| `texSoil` | R32F | `D` — soil depth, normalized | 0–1 | pedogenesis | `Wcap`, vegetation SI |
| `texVeg` | RGBA32F | `V₀..V₃` — density per species | 0–1 each | vegetation step | fauna, soil, ET shading, render |
| `texSeed` | RGBA32F | `B₀..B₃` — seed bank per species | 0–1 each | dispersal, hydrochory | establishment |

Four species is the channel budget of a single RGBA texture. Exceeding four
means a second texture or an array — worth resisting.

### 3.2 Particle state

Two ping-pong FBO pairs, `PARTICLE_TEX_DIM²` particles (start at 128² = 16 384;
256² = 65 536 is the realistic ceiling at projector resolution).

| FBO | Format | Channels |
|---|---|---|
| `fboParticlePos` | RGBA32F | `x`, `y`, `age` (sim-years), `alive` flag |
| `fboParticleVel` | RGBA32F | `vx`, `vy`, `speciesTag` (−1 = none), `tagLoad` |

`speciesTag` and `tagLoad` implement hydrochory. A particle passing through a
cell with `V_s > tag_threshold` acquires tag `s` with load 1.0; load decays per
tick and is written into `texSeed` at the particle's cell each step.

---

## 4. Update order

Order matters. Fixed sequence per tick:

```
1. updateHeight()        // from KinectProjector, only on new depth frame
2. updateGradient()      // only if height changed
3. updateObstacle()      // every frame — hands must deflect flow immediately
4. advectParticles()     // wall-clock dt
5. depositWater()        // particles → texWater.W, texSeed
6. cullParticles()       // ET death test
7. spawnParticles()      // from pucks + GUI
8. updateFieldCapacity() // texSoil → texWater.Wcap
9. updateVegetation()    // establishment, growth, competition, mortality
10. updateSeedBank()     // neighbourhood dispersal + decay
11. updatePedogenesis()  // accumulation and erosion
12. updateFauna()        // Critter agents, CPU-side
13. applyDisturbance()   // if height delta exceeded threshold this frame
14. render()
```

Steps 9–11 run at ecological cadence (`dt_sim`), steps 4–7 at frame rate.

---

## 5. Subsystem specifications

### 5.1 Gradient map

```
∇h(c)  = central-difference of texHeight, Sobel-weighted
slope  = ‖∇h‖                       // mm per cell, convert to degrees for SI
dir(c) = normalize(−∇h)             // steepest descent
```

Recompute only on new depth frames. Store `slope` in `.b` so vegetation and
erosion read it without recomputing.

### 5.2 Particle advection

Per particle, per frame:

```
g     = texGradient.sample(p.xy).xy          // steepest descent
j     = jitter_amp · noise2D(p.xy, t)        // braiding
o     = obstacleDeflect(texObstacle, p.xy)   // repel from steep edges
d     = veg_drag · texVeg.sample(p.xy).sum() // reserved, default 0

F     = w_grav·g + w_jitter·j + w_obs·o
v'    = (v + F·dt) · (1 − (drag_base + d)·dt)
v'    = clampLength(v', v_max)
p.xy += v'·dt
```

`jitter_amp` is the parameter that decides whether flow reads as a single
deterministic thread or a braided channel network. Expect to tune it more than
anything else in the module.

### 5.3 Deposition

```
W(c)      += deposit_rate · n_particles(c) · dt
W(c)       = min(W(c), Wcap(c))
B(c, s)   += tagLoad · seed_deposit_rate · dt   for tagged particles
```

Implemented as additive blending into `texWater` with point sprites at particle
positions, one draw call.

### 5.4 Evapotranspiration / particle death

Per particle per frame:

```
dryness  = 1 − W(c) / Wcap(c)
exposure = 1 + slope_exposure · slope(c)
shade    = 1 − shade_coeff · Σ V(c,s)        // clamp ≥ shade_floor

p_death  = et_base · dryness · exposure · shade · dt
kill if rand() < p_death
```

`shade` is the nurse effect. It is the second biota → substrate feedback and it
is what makes vegetated basins hold standing water while bare slopes dry.

### 5.5 Field capacity

```
Wcap(c) = wcap_bare + whc_per_depth · D(c)
```

The single line that closes the autogenic loop. Soil raises the ceiling on
water, water raises suitability, suitability admits later species.

### 5.6 Vegetation

**Suitability.** Trapezoidal SI curve per factor, four breakpoints
`(min, opt_lo, opt_hi, max)`:

```
SI(x; min, opt_lo, opt_hi, max) =
    0                              x ≤ min  or  x ≥ max
    (x − min)/(opt_lo − min)       min < x < opt_lo
    1                              opt_lo ≤ x ≤ opt_hi
    (max − x)/(max − opt_hi)       opt_hi < x < max
```

**Composite HSI**, geometric mean by default:

```
S(c,s) = ( SI_moist(W(c))  ·  SI_slope(slope(c))  ·  SI_soil(D(c)) ) ^ (1/3)
```

Set `AGGREGATE_MODE = MIN` to switch to Liebig's law of the minimum. Worth
exposing — it changes the character of the boundaries visibly, from soft
gradients to hard edges.

**Establishment:**

```
P_estab(c,s) = r_estab[s] · S(c,s) · B(c,s) · (1 − ΣV(c)) · dt_sim
```

The `(1 − ΣV)` term is unoccupied space. Establishment is gated by suitability,
propagule pressure, *and* vacancy.

**Growth** — logistic, with suitability modulating carrying capacity:

```
K(c,s)  = K_max[s] · S(c,s)
ΔV      = g[s] · V(c,s) · (1 − ΣV(c)/K(c,s)) · dt_sim
```

Shared `ΣV` in the denominator is what makes this competition rather than four
independent populations. This is where the fundamental niche becomes the
realized niche.

**Mortality:**

```
ΔV −= m[s] · (1 − S(c,s)) · V(c,s) · dt_sim
```

Mortality scales with *un*suitability, so a cell that dries out loses its late-
seral cover first if that species has the narrower curve.

### 5.7 Seed bank and dispersal

```
B(c,s) += disp_rate[s] · kernel3x3(V(·,s)) · dt_sim    // neighbourhood
B(c,s) += hydrochory deposits                          // from §5.3
B(c,s) *= (1 − seed_decay[s] · dt_sim)
B(c,s)  = min(B(c,s), B_max)
```

Pioneer species get high `disp_rate` and low `seed_decay`; late-seral get the
reverse. This is the colonization–competition tradeoff, and it is what produces
a sequence rather than a race.

### 5.8 Pedogenesis

```
accumulation = ped_rate · ΣV(c) · (1 − D(c)/D_max)
erosion      = ero_rate · slope(c) · (1 − ΣV(c)) · flowIntensity(c)
ΔD           = (accumulation − erosion) · dt_sim
```

This resolves the soil-ceiling question two ways at once. The
`(1 − D/D_max)` term gives an asymptotic cap so flat vegetated ground stops
accruing; the erosion term means steep or scoured ground never accrues in the
first place, so topography keeps mattering late in the sere.

`flowIntensity(c)` is particle count per cell, already available from §5.3.

### 5.9 Fauna

Existing `Critter` agents, CPU-side, unchanged in movement. Two additions:

```
steer  += veg_attract · ∇(ΣV)      // gradient ascent on vegetation density
target_population = fauna_per_veg · Σ_grid ΣV(c)
```

Spawn and cull toward `target_population` with hysteresis. Grazing (`V` reduced
under critters) is the two-way option and stays off by default —
`FAUNA_GRAZING = false`.

### 5.10 Disturbance

Detect per depth frame:

```
Δh(c) = |h_new(c) − h_prev(c)|
if Δh(c) > disturb_threshold:
    V(c,·) *= (1 − disturb_veg_loss)
    D(c)   *= (1 − disturb_soil_loss)
    B(c,·) *= (1 − disturb_seed_loss)
```

Moisture needs no explicit handling — it is rewritten implicitly because the
gradient map regenerates and particles reroute.

`disturb_threshold` must sit above sensor noise. Take the value from the
existing Magic Sand depth-stability filter rather than picking a new one.

### 5.11 EcosystemRenderer — mark vocabularies

Positioning first, because it constrains every choice below: the fork's
existing `heightMapShader.frag` already made and justified the one visual
decision that matters most — no hue-ramp lookup texture. Its own comment names
the reason directly: a 1D rainbow height ramp is "the single most recognizable
generic AR sandbox visual signature," and it was replaced with a procedural
organic-growth pass (lichen-like biomass thickening toward ridgelines, `fbm`
noise driving irregular patch shapes rather than smooth banding). The G-TUI
review in the reference set independently corroborates this as the field's
default failure mode: Kreylos's original AR Sandbox software — the software
most of the 199 systems it surveys are still, knowingly or not, visually
quoting — "visualize[s] elevation, contour lines, overland flow, and pooling"
through exactly that kind of ramp. Every rule in this section exists to keep
the ecosystem layers from quietly reintroducing it through the back door (a
moisture heatmap, a suitability heatmap, a species-density heatmap are all the
same mistake wearing a different variable name).

The two reference images pin down what to build *instead*, and they turn out
to be describable in the vocabulary the codebase already has:

- The photographed sandbox (green/dark-teal/warm-orange mottled through white
  sand, grainy rather than smoothly blended) is the visual signature of the
  ELF Dynamic System (Murgatroyd, Butler & Gaffney) — "large areas of bright
  green, reddish pink and dark grey represent the predominant plant type in a
  cell... individual dots of red, yellow or dark blue represent humans." Two
  things to take from it: dominant-species colour is categorical, not a
  gradient, and it reads as *stippled texture at the boundary*, not a flat
  fill with a hard edge — which is precisely the `fbm`-patchwork technique
  `heightMapShader.frag` already uses for bare-substrate coverage, just needs
  turning into a real per-species point field instead of a decorative one.
- The software-interface screenshot (contour rings, a particle-speckled flow
  channel, a species-select dropdown, a planting cursor) is Liu's Tangible
  River Table (`Augmented_Reality_Fluvial.pdf`, Fig. 4/6): "the user can
  directly select which aspects to show on the table," and separately,
  "adjust the parameters in the computational model by selecting the plant
  species and properties, to test where to send them." That is this fork's
  `PuckTracker` + an ImGui species-select panel, not a new piece of hardware.

#### 5.11.1 Layer stack

Draw order for `render()` (§4 step 14), each a separate pass/draw call over
`fboProjWindow` unless noted:

```
1. ground pass         existing heightMapShader, extended in place with the
                        soil wash and bare-growth fade — see 5.11.2. One pass,
                        reads texSoil and texVeg alongside texHeight.
2. hydrology linework  renderLinework.frag — flow streaks + moisture isolines
3. vegetation stipple  renderPoints.frag — per-species point sprites from texVeg
4. mycelium glow       existing MyceliumNetwork pass, unchanged
5. fauna marks         existing Critter triangles + ring, unchanged
6. elevation contours  existing black contour pass, kept as the coarse
                        orientation reference — drawn last, thin, low-alpha
7. interaction overlay planting cursor / puck ring / species ghost — screen-
                        space, topmost — see 5.11.6
```

Layers 2–3 and the extension folded into 1 are the new work; the rest of 1 and
4–6 already exist and are reused as-is.

#### 5.11.2 Base ground — retiring the lichen pass gracefully

The existing procedural growth pass currently owns the entire ground colour,
blending a substrate colour (`rockColor`, elevation-only) with a decorative
lichen colour through an `fbm`-driven mask (`growthAmount`). Two small changes
to that one shader fold both the soil wash and the handoff to real vegetation
in, rather than adding separate blended passes:

```
rockColor     = mix(darkRock, lightRock, elevationNorm) * soilTint   // §5.11.5, real D(c)
growthAmount *= 1.0 − smoothstep(0.0, MIN_DRAW_DENSITY, ΣV(c))       // decorative lichen recedes
finalGround   = mix(rockColor, existingGrowthColor, growthAmount)
```

The substrate colour picks up real soil depth directly; the decorative mask
recedes as real vegetation takes over, so it never competes with an actual
stipple sitting on top of the same cell. `MIN_DRAW_DENSITY` is shared with
§5.11.3 so the handoff is exact: the moment a cell has enough real vegetation
to draw a single stipple point, the procedural lichen texture there has fully
receded. No point in build order (§9) where both are visible at full strength
in the same cell.

#### 5.11.3 Vegetation stipple — `renderPoints.frag`

Per-species density is rendered as a jittered field of small point sprites,
not a flat alpha-blended fill and not a hue-blended average — the photographed
reference reads as *individual coloured dots*, including at ecotones where two
species overlap, and averaging their colours there would produce a muddy
intermediate hue no species in `species_<biome>.json` actually has.

```
for each candidate point p in a STIPPLE_DENSITY-per-cell jittered grid
    (jitter seeded by hash(cellCoord), stable frame-to-frame — points must
    not swim, only appear/fade/resize as V changes):

    w[s]    = V(c, s)  for s in 0..3
    totalV  = Σ w[s]
    if totalV < MIN_DRAW_DENSITY: discard

    s       = weighted_random(w, hash(p))   // one species per point, not blended
    alpha   = smoothstep(MIN_DRAW_DENSITY, FULL_COVER_DENSITY, totalV)
    size    = mix(SPRITE_MIN, SPRITE_MAX, V(c,s) / K_max[s])
    tint    = SPECIES_COLOR[s] * mix(0.55, 1.0, S(c,s))   // suitability dims, never hue-shifts
    draw irregular blob sprite (fbm-perturbed radial falloff, reuse the
        hash()/valueNoise()/fbm() already defined in heightMapShader.frag —
        move them to a shared bin/data/shaders/eco/noise.glsl include) at p,
        colour tint, alpha
```

Suitability modulates *value*, never hue — a struggling population should
read as dim, not as a different species. This keeps four simultaneously
overlapping populations legible instead of collapsing into a colour-mixing
mess as soon as competition gets interesting, which is exactly the scenario
(§5.6, shared `ΣV` denominator) the module is built to make legible.

**Species palette** — the photographed reference and the ELF paper's own
description agree closely enough to take directly as the starting family:
pioneer bright yellow-green, second warm ochre/orange, third deep teal-green,
late-seral near-black canopy green. Store as a `color` field alongside each
species' `source` citation in `species_<biome>.json` (extends §8's per-
parameter provenance pattern) so a literature-sourced species and its visual
identity travel together and swapping `species_<biome>.json` (per §7, "one of
the swappable design decisions") re-skins the installation for free.

#### 5.11.4 Hydrology linework — `renderLinework.frag`

Two independent line families, kept visually distinct so participants can
learn "line colour = abiotic data" as a stable grammar (§5.11.7):

- **Flow.** Particles (§5.3) render as short streaks aligned to velocity, not
  round dots — legible direction without a separate vector-field overlay, the
  same "water-like manner" read the Sound Sandbox paper's LBM particle stream
  aims for. Colour pale white-blue; alpha modulated by `age` so fresh
  deposition reads brighter than lingering residue. A hydrochory-tagged
  particle (`speciesTag ≥ 0`, §3.2) additionally tints toward
  `SPECIES_COLOR[speciesTag]` at low intensity — the seed-carrying fraction of
  the flow becomes visible as a faint colour bleeding off the vegetated cell
  it tagged in, which is the only place in this system hydrochory (§5.7) is
  otherwise invisible.
- **Moisture isolines.** A second, independent contour pass over `W(c)/Wcap(c)`
  at fixed bands (default 0.25/0.5/0.75), warm orange, thinner and lower-alpha
  than the existing black elevation contours so the two never compete —
  directly the Tangible River Table screenshot's multi-layer convention
  (elevation guidelines + a second data-specific line colour shown
  simultaneously, user-selectable). Reuses the existing marching-squares logic
  in `heightMapShader.frag`'s contour block against a different sampler.

#### 5.11.5 Soil wash — `renderSoilWash.frag`

Named as its own shader in §7 for where the logic lives (it is a pure function
of `texSoil`, factored out for reuse), but consumed as a tint on the ground
pass's substrate colour (§5.11.2) rather than a separate multiply-blended draw
call — soil is a substrate property, not an overlay:

```
soilTint = mix(vec3(1.0), vec3(0.25, 0.16, 0.10), D(c) / D_max)
```

Deliberately subtle — mainly legible at the soil/bare-sand ecotone edge, where
it reinforces the facilitation-model read (§1.1) without adding a bright hue
that would compete with the vegetation stipple sitting on top of it.

#### 5.11.6 Interaction overlay

Screen-space, topmost, drawn from `PuckTracker` state — one physical puck
functions as the planting cursor, species chosen through an ImGui dropdown
(matching the reference screenshot's own `SELECT SPECIES` control and this
fork's existing ofxImGui-based GUI, rather than the multiple colour-coded
pucks read literally out of §7's `PuckTracker.h` comment — one puck plus a
software species selector is simpler, already buildable on the existing
single-puck detector, and is what the reference software actually shows).

- **Puck ring.** Reuses the existing persistent-ring marker (see the repo's
  ring-rendering history), recoloured to `SPECIES_COLOR[selectedSpecies]` so
  the currently-armed species is visible at a glance without reading the GUI.
- **Dispersal-radius ghost.** A faint dashed ring at `disp_rate[selectedSpecies]`-
  derived radius around the puck, matching the screenshot's dashed ring —
  shows participants the seed shadow they are about to create before they
  commit to a placement, rather than a mechanic they have to place-and-check
  to understand.

#### 5.11.7 Palette and grammar rules

- No hue-ramp lookup texture anywhere in the eco layers — `heightColorMapSampler`
  stays retired for the same reason it was retired in §5.11's opening.
- Hue encodes identity (which species; black vs orange for elevation vs
  moisture data); value/brightness encodes state (how suitable, how much);
  never the reverse. This is the one rule that keeps four species and two
  independent contour families simultaneously legible on screen at once.
- Each species' colour is fixed for the life of a `species_<biome>.json`
  preset (§5.11.3) — swapping the biome swaps the palette as a unit, never
  piecemeal at runtime.

#### 5.11.8 Rendering parameters

| Symbol | Name | Unit | Range | Default |
|---|---|---|---|---|
| `STIPPLE_DENSITY` | candidate points per cell | # | 1–8 | 3 |
| `MIN_DRAW_DENSITY` | vegetation visibility floor, shared with 5.11.2's `bareAlpha` | ΣV | 0–0.2 | 0.05 |
| `FULL_COVER_DENSITY` | ΣV at full sprite alpha | ΣV | 0.3–1 | 0.7 |
| `SPRITE_MIN` / `SPRITE_MAX` | sprite radius | cell | 0.3–1.5 | 0.4 / 1.1 |
| `MOISTURE_ISOLINE_BANDS` | isoline count over `W/Wcap` | # | 1–5 | 3 |
| `FLOW_STREAK_LEN` | particle streak length | cell | 0.5–3 | 1.2 |
| `HYDROCHORY_TINT_STRENGTH` | tagged-particle colour bleed | 0–1 | 0–1 | 0.35 |

Build order note (ties into §9): ship `renderPoints.frag` alongside step 4
(single-species vegetation), not after — a species with no visible mark
vocabulary is unverifiable by eye, which is the whole point of steps 1–5
being watched closely. Gate the base-ground handoff (§5.11.2) in at step 5,
since soil is the first point real vegetation exists to hand off to.

---

## 6. Parameters

Time unit is sim-years throughout. Ranges are starting brackets for tuning, not
literature values — those get sourced per §8.

### Hydrology (wall-clock)

| Symbol | Name | Unit | Range | Default |
|---|---|---|---|---|
| `w_grav` | gravity weight | — | 0.5–5 | 2.0 |
| `w_jitter` | jitter weight | — | 0–1 | 0.15 |
| `jitter_amp` | jitter amplitude | cell | 0–2 | 0.5 |
| `w_obs` | obstacle repulsion | — | 0–10 | 4.0 |
| `drag_base` | velocity damping | s⁻¹ | 0.1–5 | 1.0 |
| `v_max` | max particle speed | cell·s⁻¹ | 1–50 | 15 |
| `deposit_rate` | residence → water | s⁻¹ | 0.01–1 | 0.1 |
| `et_base` | base death probability | s⁻¹ | 0.001–0.1 | 0.02 |
| `slope_exposure` | slope ET multiplier | deg⁻¹ | 0–0.1 | 0.02 |
| `shade_coeff` | vegetation ET reduction | — | 0–1 | 0.6 |
| `shade_floor` | min shade multiplier | — | 0.1–1 | 0.3 |
| `spawn_rate` | particles per puck | s⁻¹ | 10–500 | 100 |

### Soil

| Symbol | Name | Unit | Range | Default |
|---|---|---|---|---|
| `wcap_bare` | bare-sand field capacity | — | 0.05–0.3 | 0.15 |
| `whc_per_depth` | soil → capacity | — | 0.1–1 | 0.5 |
| `ped_rate` | organic accumulation | yr⁻¹ | 0.001–0.05 | 0.01 |
| `ero_rate` | erosion coefficient | yr⁻¹deg⁻¹ | 0.001–0.05 | 0.005 |
| `D_max` | max soil depth | — | 1.0 | 1.0 |

### Per species (×4)

| Symbol | Name | Unit | Pioneer | Late-seral |
|---|---|---|---|---|
| `r_estab` | establishment rate | yr⁻¹ | 0.8 | 0.15 |
| `g` | growth rate | yr⁻¹ | 0.6 | 0.2 |
| `m` | mortality rate | yr⁻¹ | 0.3 | 0.08 |
| `K_max` | max density | — | 0.4 | 1.0 |
| `disp_rate` | dispersal rate | yr⁻¹ | 0.5 | 0.1 |
| `seed_decay` | seed bank decay | yr⁻¹ | 0.1 | 0.5 |
| `SI_moist` | moisture curve | 4-tuple | wide | narrow |
| `SI_slope` | slope curve | 4-tuple, deg | wide | narrow |
| `SI_soil` | soil curve | 4-tuple | `(0, 0, .3, .7)` | `(.4, .7, 1, 1)` |

The `SI_soil` row is the whole mechanic in one line. Pioneers require no soil and
are *excluded* by deep soil; late-seral species cannot establish until soil
exists. That asymmetry is the facilitation model.

### Disturbance

| Symbol | Name | Unit | Range | Default |
|---|---|---|---|---|
| `disturb_threshold` | height delta trigger | mm | 3–20 | from depth filter |
| `disturb_veg_loss` | vegetation destroyed | — | 0.5–1 | 0.9 |
| `disturb_soil_loss` | soil destroyed | — | 0.3–1 | 0.7 |
| `disturb_seed_loss` | seed bank destroyed | — | 0–0.8 | 0.3 |

Seeds surviving disturbance better than standing vegetation is correct — a
persistent seed bank is how real systems recover fast after disturbance.

### Rendering

See §5.11.8 for the full table and how each parameter is used.

---

## 7. Code layout

Proposed, adapt to actual fork structure:

```
src/Ecosystem/
  EcosystemManager.{h,cpp}     // owns layers, enforces update order
  EcosystemParams.{h,cpp}      // struct, JSON serialize, ImGui binding
  HydrologyLayer.{h,cpp}       // gradient, obstacle, particle system
  VegetationLayer.{h,cpp}      // SI curves, establishment, growth, seed bank
  SoilLayer.{h,cpp}            // pedogenesis, field capacity
  FaunaLayer.{h,cpp}           // wraps existing Critter
  PuckTracker.{h,cpp}          // color blob → spawn sources
  EcosystemRenderer.{h,cpp}    // mark vocabularies, compositing

bin/data/shaders/eco/
  noise.glsl     shared hash()/valueNoise()/fbm(), factored out of
                  heightMapShader.frag so §5.11's eco passes reuse it
                  instead of re-deriving their own
  gradient.frag  obstacle.frag
  particleAdvect.frag  particleDeposit.frag  particleCull.frag
  vegetation.frag  seedbank.frag  pedogenesis.frag
  renderLinework.frag  renderPoints.frag  renderSoilWash.frag

bin/data/eco/
  params_default.json
  species_<biome>.json
```

`EcosystemParams` as a single JSON-serializable struct is what makes A/B testing
tractable — save a parameter set, reload it, compare. Given the toolkit framing,
this file *is* one of the swappable design decisions.

`PuckTracker` here already exists in `src/Games/PuckTracker.{h,cpp}` as a
single shape-detected puck (no colour-blob multi-puck tracking yet — see
§5.11.6 for why the interaction design leans into that instead of building the
colour-blob version this section's original comment implies).

---

## 8. Sourcing parameters from literature

To support the data-relationship claim, the per-species block should be filled
from published values for one named biome rather than tuned by eye. Practical
sources, in rough order of directness:

- **USFWS Habitat Suitability Index model series** — published SI curves in
  exactly the four-breakpoint form used in §5.6, per species per factor
- **TRY Plant Trait Database** — growth rate, seed mass, dispersal distance
- **USDA PLANTS Database** — moisture, shade, and salinity tolerance classes,
  plus establishment characteristics
- **LEDA Traitbase** (NW European flora) — seed longevity and dispersal
- Primary-succession chronosequence literature for `ped_rate` — glacial
  forelands (Glacier Bay) and volcanic substrates (Mount St. Helens, Surtsey)
  are the classic sites and give measured soil-accumulation rates

Record the source per parameter in `species_<biome>.json` as a `source` field.
That file becomes the evidence for the claim that the rules are data-derived
while the place is invented. Per §5.11.3, give each species a `color` field
alongside its `source` fields — the visual identity is as much a per-biome,
swappable decision as the growth-rate literature is.

---

## 9. Build order

1. Gradient map + particle advection + point rendering. No ecology. Verify flow
   reads correctly and hands deflect it.
2. Deposition and ET death. Verify moisture accumulates in basins and dries on
   slopes; tune `jitter_amp` here.
3. Puck tracking as spawn source.
4. Single-species vegetation on moisture and slope only. No soil, no seed bank.
   Ship `renderPoints.frag` (§5.11.3) alongside this step, not after — a
   species with no visible mark vocabulary can't be watched, which is the
   point of keeping steps 1–5 this incremental.
5. Soil layer and field-capacity feedback. This is the first point the system is
   actually successional — everything before it is equilibrium. Also the point
   at which the base-ground handoff (§5.11.2) gates the old lichen pass by `ΣV`.
6. Seed bank and dispersal.
7. Second species with contrasting `SI_soil`. First visible sere.
8. Hydrochory tagging. Verify the flow-linework tint (§5.11.4) makes the
   tagged fraction of particles visible.
9. Fauna coupling.
10. Species 3 and 4, literature-sourced parameters.

Steps 1–5 are the minimum defensible system. Everything after is refinement.
