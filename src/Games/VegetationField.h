/***********************************************************************
VegetationField.h - a per-cell flora simulation and its ELF-style visual
encoding: three plant "types" that grow within elevation bands relative
to a tunable water/snow line, ported from the cellular growth rules in
Europe's Lost Frontiers' BDlocation/BDenvironment (see
ELFdev001/ELFDynamicSystem - the "ecosim model" this is patterned on).

Deliberately matches ELF's actual mechanics rather than a stylized
approximation of them, per a direct line-by-line comparison against
BDlocation.java/BDenvironment.java:

- One cell per kinect pixel (GRID_STEP == 1 in the .cpp), not a coarser
  decimated grid - ELF's cell IS one native Kinect depth pixel; its
  CELLWIDTH/CELLHEIGHT=4 only upscales the *display*, never the
  sampling.
- Growth is a per-tick coin flip, not continuous accumulation - matching
  BDlocation.growShrubs()/growFruits()/growNuts() literally:
  Math.random()*100 < SHRUBGROWTH/FRUITGROWTH/NUTGROWTH (1/3/2), and on
  success density rises by exactly 1/255, capped at 255 (1.0 here). One
  update() call is treated as one ELF tick, same convention Critter/
  HumanAgent already use - see those files' header notes. This was
  previously reimplemented as continuous `density += rate * dt`, an
  earlier session's unprompted smoothness choice never put to the
  project owner as a decision; reverted at their explicit request
  (2026-09) to have a known ELF-faithful baseline to diagnose the
  "vegetation never grows" problem from, before any further
  customization. Fruit is still the fastest grower, shrub the slowest,
  so in the wide middle elevation band where all three are eligible,
  fruit tends to win the color out over time exactly as it does in ELF -
  just glacially slowly now, matching ELF's real pace (tens of thousands
  of ticks to reach full density), not the few-seconds pace the
  continuous version had.
- One-way growth on land: a channel only ever rises (while its band
  condition holds) or holds steady (while out of band but still land);
  it never decays just from drifting out of a band. Only an actual
  transition to water or snow resets all three to 0 instantly - see
  BDlocation.makeSnow()/makeWater(), which zero shrubs/fruits/nuts
  outright with no easing, and stepCells(), whose growShrubs() etc.
  calls are the *only* per-tick modifications to those counts (nothing
  in ELF's source path ever decrements them for leaving a band while
  still land).

SPREAD LAYER (2026-09, succession-model phase, deliberate departure from
ELF - see SPREAD_RADIUS_MM/SPREAD_CHANCE_MULTIPLIER below): the growth
above is otherwise exactly ELF's, and ELF's own mechanic has no
cell-to-neighbor coupling at all - a cell's growth chance depends only
on its own elevation, the shared climate, and a private random roll,
never on what's growing next to it. That's a deliberate, explicit
divergence point from a true cellular automaton (a CA's defining trait
is exactly that neighbor coupling), confirmed by reading BDlocation's
source directly rather than assuming "cell" implied CA dynamics - ELF's
"cell" means spatial discretization, not neighbor-rule dynamics.
hasEstablishedNeighbor() adds that coupling on top, as the first piece
of the ecological-succession phase (see CLAUDE.md's "Future phase"
section): a same-species neighbor within a real-world radius multiplies
that species' own growth chance, preserving the existing 1:3:2 shrub:
fruit:nut ratio as the spread weight rather than adding a second
competition parameter. The plain spontaneous chance is kept as a
fallback specifically because the hydrology/particle-flow/seeding layer
(also part of that same future phase, not yet built) doesn't exist yet
to plant genuine first seeds - without it, a strict neighbor-only rule
could never bootstrap growth on untouched sand at all.

ELEVATION IS NORMALIZED, NOT RAW MILLIMETERS - this is the one place an
earlier pass of this port genuinely diverged from ELF's actual physics
rather than just its visual style, and it is the reason a hand-dug pit
or a mounded hill could fail to register as water/growth/snow at all
depending on the installation: BDlocation's "cellheight" is never a
real-world measurement. It is raw Kinect depth rescaled once into a
fixed 0..255 range via BDlocation's own hardcoded MINDEPTH=6800/
MAXDEPTH=8200, and every threshold BDenvironment compares against it
(temperature, LIVINGRANGE, SHRUBLINE, FRUITLINE, NUTLINE) is a point on
that same 0..255 scale - i.e. a *fraction* of whatever physical depth
range the installation was calibrated for, not an absolute millimeter
distance. Comparing elevationAtKinectCoord()'s real millimeters against
invented millimeter constants (an earlier version of this file did
exactly that) has no principled relationship to either ELF's real
numbers or to a given box's actual usable relief - a 60mm shift might
be negligible on one installation and flood an entire box on another.

setElevationRange() fixes this: it takes a calibrated elevation range
(elevationMin/elevationMax, mm) rather than an invented one. ofApp
defaults this from KinectProjector::getCalibratedCeilingElevation() -
Magic Sand's own already-calibrated ceiling for this specific
installation (the same plane-fit routine as the base plane, just
sampled at the box's real ceiling height), mirrored to a symmetric
floor since Magic Sand doesn't calibrate an equivalent dig-depth limit.
This is the same physical box and the same Magic Sand calibration this
fork inherited unchanged - no reason to re-guess a range when a real
per-installation measurement already exists, even if only for one
side. SandSurfaceRenderer's own elevationMin/elevationMax (derived from
the loaded colormap's declared height range, used there to normalize
elevationNorm for cosmetic color modulation in heightMapShader.frag -
see that file's header note) is a different, generic value not tied to
this box's real calibration, and is no longer what this class defaults
from. All classification here normalizes elevation into that
same 0..1 fraction before comparing against thresholds, so this class's
MINDEPTH/MAXDEPTH analog is shared with, not disconnected from, the
shader's own normalization. SHRUB_LINE_RATIO/FRUIT_LINE_RATIO/
NUT_LINE_RATIO are BDenvironment's literal SHRUBLINE=20/FRUITLINE=60/
NUTLINE=25 each divided by LIVINGRANGE=200 - i.e. 0.10/0.30/0.125,
expressed as fractions OF THE LIVING RANGE rather than of the total
calibrated range (see LIVING_RANGE_FRACTION's own note just below for
why that distinction matters). Note in particular that FRUITLINE
(0.30, the *largest* offset) makes fruit ELF's most exclusive/narrowest
band and NUTLINE (0.125) makes nut almost as permissive as shrub's
SHRUBLINE (0.10) - a shape an earlier pass of this port also got
backwards by using symmetric, similarly-sized offsets for fruit and nut.

LIVING_RANGE_FRACTION is 0.368 (2026-09-25), departed from ELF's literal
LIVINGRANGE=200/255 (~0.78) a second time, per explicit user
confirmation - the first revert (2026-09-24, matching ELF's literal
value) turned out impractical on real hardware, this time confirmed by
direct measurement rather than just the earlier general finding: on
this box's calibrated range (-142.606..142.606mm), the user's own
maximum hand-dig only reaches -41.8mm, and even TEMPERATURE pushed to
its practical ceiling of 1.0 (higher makes snow literally unreachable)
only gets the water line to -81mm at ELF's literal fraction - no amount
of TEMPERATURE tuning alone can close a 39mm+ gap, confirming
LIVING_RANGE_FRACTION itself had to move, not just BASE_TEMPERATURE.
0.368 is solved directly from two real measurements (dig limit -41.8mm,
mound limit +80mm, from which BASE_TEMPERATURE=0.745 is also solved -
see that field's comment) rather than guessed the way the original 0.5
departure was.

SHRUB/FRUIT/NUT_LINE are ratios OF LIVING_RANGE_FRACTION rather than
fixed fractions of the total range specifically so they stay correct
regardless of what LIVING_RANGE_FRACTION is set to (ELF's own literal
value now, or a loosened one again later): fruit's band width is
livingRange - 2*fruitLineOffset. At ELF's own numbers that's
0.78 - 2*0.235 ~= 31% of the range - plenty. Holding FRUITLINE fixed at
0.235 of the *total* range while shrinking living range to 0.5 (as a
previous, now-reverted departure did) crushed that band to
0.5 - 2*0.235 = 0.03 - about 3% of the range, a near-hairline a real
pixel rarely lands in - confirmed on real hardware as "little to no red
[fruit] ever appears." Deriving FRUIT_LINE_FRACTION = FRUIT_LINE_RATIO *
LIVING_RANGE_FRACTION each frame instead keeps fruit's band at the same
~31%-of-living-range width ELF's own ratio implies, however
LIVING_RANGE_FRACTION itself is tuned.

TEMPERATURE/BASE_TEMPERATURE/MAX_TEMPERATURE_OFFSET are themselves
fractions of the same calibrated range (0..1-ish, not millimeters).
BASE_TEMPERATURE's starting value is deliberately NOT ELF's literal
default (temperature=200/255, i.e. exactly LIVING_RANGE_FRACTION) -
confirmed directly from BDenvironment.stepCells() that this exact value
makes water mathematically unreachable (`height < temperature -
LIVINGRANGE` becomes `height < 0`), and confirmed via incTemp()/
decTemp() (ELF's 'q'/'a' operator keys - incTemp() is what RAISES
temperature, not decTemp(), an earlier version of this note had the
direction backwards) that ELF's own design expects an operator to raise
temperature above LIVINGRANGE at session start specifically to open a
reachable water band - ELF ships in a deliberate zero-water cold start,
not a bug this port needs to avoid reproducing. A real ELF reference
figure the user provided directly confirms this: captioned "ELF Dynamic
System with HIGHER TEMPERATURES... areas under water," i.e. that
figure's visible water required an operator-raised temperature, this
exact mechanism.

Set to 0.745 (2026-09-25), superseding an earlier first-guess of 0.9 -
still using ELF's own lever (temperature above LIVING_RANGE_FRACTION)
rather than a new one, but now solved directly from two real hardware
measurements instead of guessed. The 0.9/LIVING_RANGE_FRACTION=0.784
pairing put the water line at -104.5mm, confirmed unreachable (max
hand-dig -41.8mm) with no fix available from TEMPERATURE alone (see
LIVING_RANGE_FRACTION's comment for why). With both fields now solved
together from measured targets - water line -35mm (7mm margin under
the -41.8mm dig limit) and snow line +70mm (10mm margin under a
measured +80mm mound limit) - on this box's calibrated range
(-142.606..142.606mm): snowLevelFrac = TEMPERATURE directly gives
0.745, and waterLevelFrac = TEMPERATURE - LIVING_RANGE_FRACTION gives
LIVING_RANGE_FRACTION = 0.368.
This is still a per-installation placement, same as ELF's own operator
dialing 'q'/'a' at setup time, just measured properly this time rather
than guessed - if this box's calibration or physical sand depth changes
later, both values should be re-derived the same way: measure the real
dig/mound limits via the Vegetation panel's raw elevation readout, then
solve for TEMPERATURE/LIVING_RANGE_FRACTION from those two targets
directly, rather than hand-tuning sliders by feel.

A CPU-side grid sampled from elevationAtKinectCoord() each frame,
uploaded as a single texture that SandSurfaceRenderer's heightMapShader
reads - see getTexture() /
getGridOrigin() / getGridStep(). The shader (not this class) is
responsible for turning R/G/B density into a color: it picks whichever
channel is strictly largest and colors the whole cell that plant's flat,
elevation-modulated color - see heightMapShader.frag - matching
BDlocation.getCellColor()'s winner-take-all comparison, except on a tie
(including the initial all-zero state, which is the common case - most
of the play area starts, and often stays, unvegetated): getCellColor()
returns flat Color.PINK there - confirmed by actually running ELF's own
TESTING-mode reference build, where untouched land shows as solid,
unmodulated pink exactly as the source implies - but this fork
deliberately renders that case as negative space instead, a project
owner's explicit stylistic choice made with that confirmation in hand,
not a fidelity claim. See heightMapShader.frag's header note. Density
itself only decides the
comparison outcome here, never a fade amount - there is no partial/faded
color state in ELF, and now none in this shader either.

TEMPERATURE shifts both the water and snow lines by the same amount,
matching ELF's temperature effect exactly (raising it both floods more
land and shrinks the snowcap, since the snow threshold rising means
fewer cells clear it) - see BDenvironment.stepCells()/incTemp()/decTemp().
Unlike ELF, where temperature only moves on operator keypresses (q/a),
TEMPERATURE here is not a direct control at all - it is derived each
frame from how much the sand is actively being reshaped (see
activityLevel/update()), so it is a genuinely participant-facing effect
rather than an operator-only lever, at the deliberate cost of exact
parity with ELF's own input model. Sustained sculpting raises it toward
BASE_TEMPERATURE + up to MAX_TEMPERATURE_OFFSET; an undisturbed sandbox
eases it back down toward BASE_TEMPERATURE. TEMPERATURE_EASE_RATE keeps
this a gradual "warming/cooling" rather than a frame-to-frame jitter
reacting to sensor noise.

ACTIVITY_NOISE_FLOOR (mm, not a fraction - it's compared against a raw
per-cell elevation delta before any normalization) exists because
"reacting to sensor noise" is not just a jitter risk but a real failure
mode on physical Kinect hardware: depth measurement noise of a few mm
per pixel is present on every cell every frame, even when nothing is
touching the sand - unlike genuine sculpting, which is confined to
whatever the hand is actually touching, this noise is not diluted by
averaging over the whole grid. Left unfiltered it pins activityLevel
just above zero permanently, which saturates TEMPERATURE at
BASE_TEMPERATURE + MAX_TEMPERATURE_OFFSET on a perfectly still sandbox.
Per-cell deltas at or below this floor are dropped from the activity sum
entirely (not merely damped), so a still sandbox's activityLevel reads
as exactly 0 and TEMPERATURE eases back to BASE_TEMPERATURE, matching
ELF's undisturbed baseline.

Ecosystem extension, part of the Shifting Sands fork of Magic Sand.
***********************************************************************/

#pragma once
#include "ofMain.h"
#include "ofxOpenCv.h"
#include "ofxCv.h"

#include "../KinectProjector/KinectProjector.h"

class VegetationField {
public:
	void setup(std::shared_ptr<KinectProjector> const& k);

	// The calibrated real-world elevation range (mm) elevation is
	// normalized against before any threshold comparison - see the header
	// note. ofApp calls this once at startup with a range built from
	// KinectProjector::getCalibratedCeilingElevation() - this box's own
	// already-calibrated Magic Sand ceiling - not from
	// SandSurfaceRenderer::getElevationMin()/getElevationMax() (a generic,
	// colormap-derived value unrelated to this installation's real
	// calibration). Order doesn't matter - normalizedElevation() sorts by
	// actual value rather than trusting minMM<maxMM. Also live-tunable
	// from the Vegetation GUI panel afterward; safe to call again if
	// calibration changes, since classification just picks up the new
	// range next update() - no grid reset needed.
	void setElevationRange(float minMM, float maxMM);

	// Resets the persistent density grids (only) when the play area's grid
	// dimensions actually change - an ROI update that doesn't change
	// cols/rows leaves current vegetation cover untouched.
	void setKinectROI(ofRectangle & KROI);
	void update();
	void drawGui();

	ofTexture & getTexture() { return combinedTex; }
	// Kinect-pixel-space origin and step size of the grid the texture
	// represents, for converting a shader's kinect pixel coordinate into
	// a texel lookup: (kinectCoord - getGridOrigin()) / getGridStep().
	ofVec2f getGridOrigin() const { return ofVec2f(kinectROI.x, kinectROI.y); }
	float getGridStep() const { return (float)step; }

	// Agent interaction: Critter ("deer") and HumanAgent query/consume
	// vegetation at their own location, mirroring BDdeer/BDagent reading
	// and zeroing cells[myX][myY]'s shrubs/fruits/nuts directly in
	// BDenvironment.stepDeer()/stepAgents(). kx/ky are kinect pixel
	// coordinates, same space as the rest of this class.

	// Deer eat shrub if present, else fruit, else nothing - matches
	// stepDeer()'s "if (theseshrubs>0) ... else if (thesefruit>0) ..."
	// order. Returns food gained (0 if neither was present) and zeros
	// whichever channel was eaten at that cell.
	float eatShrubOrFruit(float kx, float ky);

	// Humans try their less-taken type first (preferNut selects which),
	// falling back to the other - matches stepAgents()'s
	// fruittaken<nutstaken/nutstaken<fruittaken preference chain exactly
	// (that four-branch chain collapses to this try-one-then-the-other
	// shape since its two preference branches are mutually exclusive and
	// its two fallback branches only re-check the same two channels).
	// Returns food gained (0 if neither channel had any density) and sets
	// ateNut to say which channel was actually eaten, so the caller knows
	// whether to credit fruittaken or nutstaken.
	float eatFruitOrNut(float kx, float ky, bool preferNut, bool & ateNut);

	// For agent movement/fishing checks - matches BDlocation.getWater()/getSnow().
	bool isWaterAt(float kx, float ky) const;
	bool isSnowAt(float kx, float ky) const;

	// Grid dimensions, for Critter/HumanAgent's pickX()/pickY() equivalents
	// and to convert their own grid coordinates back to kinect pixel space.
	int getCols() const { return cols; }
	int getRows() const { return rows; }

	// Average |elevation change| per cell last frame (mm) - what
	// TEMPERATURE is actually derived from. Exposed for the GUI so an
	// operator can see what's driving it, not for agents to query.
	float getActivityLevel() const { return activityLevel; }

	// Scales a full (1.0) density's worth of eaten plant into food -
	// matches ELF's counts being added to food directly on a 0..255 scale.
	static float FOOD_PER_FULL_CELL;

	// Tunable in the debug GUI. TEMPERATURE/BASE_TEMPERATURE/
	// MAX_TEMPERATURE_OFFSET are fractions of the calibrated elevation
	// range (see setElevationRange() and the header note), not millimeters.
	static float TEMPERATURE;             // fraction, shifts both water and snow lines together - computed each frame; not a direct slider
	static float BASE_TEMPERATURE;        // fraction, TEMPERATURE's resting value when the sand is undisturbed - needs on-site placement, see the header note
	static float ACTIVITY_TO_TEMPERATURE; // fraction of TEMPERATURE offset per mm of average per-cell elevation change
	static float MAX_TEMPERATURE_OFFSET;  // fraction, clamps how far activity alone can push TEMPERATURE above BASE_TEMPERATURE
	static float TEMPERATURE_EASE_RATE;   // how fast TEMPERATURE chases its activity-driven target, per second
	// mm, per-cell elevation change below which a frame's delta is treated as
	// depth-sensor noise rather than real sculpting and dropped from the
	// activity sum entirely - see the header note.
	static float ACTIVITY_NOISE_FLOOR;

	// BDenvironment's literal LIVINGRANGE/255 - see the header note (a
	// previous session's departure to 0.5 was reverted 2026-09-24 per
	// explicit user instruction to match ELF's defaults first).
	static float LIVING_RANGE_FRACTION;

	// SHRUB_LINE_RATIO/FRUIT_LINE_RATIO/NUT_LINE_RATIO are each species'
	// water/snow-line offset expressed as a fraction OF LIVING_RANGE_FRACTION
	// (not of the total calibrated range) - BDenvironment's literal
	// SHRUBLINE=20/FRUITLINE=60/NUTLINE=25 each divided by LIVINGRANGE=200,
	// i.e. 0.10/0.30/0.125. This is what actually preserves ELF's relative
	// band shape (fruit narrowest, shrub widest, nut nearly as wide as
	// shrub - see the header note) once LIVING_RANGE_FRACTION is loosened
	// away from ELF's own value: fixing these as fractions of the total
	// range instead would have fruit's band width shrink to near-nothing
	// as living range narrows (confirmed on real hardware - almost no
	// fruit/red ever appeared once living range was loosened to 0.5,
	// because 2xFRUIT_LINE_FRACTION had, by fixed-total-range accounting,
	// come to consume nearly the entire narrowed living range). The actual
	// fractions-of-total-range used in update()'s comparisons are derived
	// from these each frame - see the .cpp.
	static float SHRUB_LINE_RATIO;
	static float FRUIT_LINE_RATIO;
	static float NUT_LINE_RATIO;

	// Percent chance per tick (one update() call) of a +1/255 density
	// increment while a cell is in-band - ELF's literal
	// SHRUBGROWTH=1/FRUITGROWTH=3/NUTGROWTH=2, not a rescaled rate. See
	// the header note. This is the SPONTANEOUS rate - what a cell uses
	// when no same-species neighbor is established nearby - not the
	// spread-boosted rate (see SPREAD_CHANCE_MULTIPLIER below).
	static float SHRUB_GROWTH_CHANCE_PCT;
	static float FRUIT_GROWTH_CHANCE_PCT;
	static float NUT_GROWTH_CHANCE_PCT;

	// CA-style spread layer, added on top of ELF's per-cell-independent
	// growth (see the header note's "Growth" section) - the first piece
	// of the succession-model phase.
	//
	// Runtime kill switch for the whole layer, purely for A/B performance
	// testing on real hardware: this session's fix for a measured 30-60x
	// slowdown (see the header note and CLAUDE.md) got 1-2fps back up to
	// only ~5fps, not the expected ~60 - meaning either the spread layer
	// is still the bottleneck (in which case this toggled off should
	// restore full frame rate immediately) or something else is (in
	// which case it won't), without needing another rebuild to find out.
	static bool ENABLE_SPREAD;
	// Real-world mm radius to search for
	// an already-established same-species neighbor before granting the
	// spread-boosted chance below; converted to a grid-cell radius via
	// the cached mmPerCell scale (see setKinectROI()) so it stays
	// meaningful regardless of a given installation's Kinect resolution.
	static float SPREAD_RADIUS_MM;
	// Multiplier applied to a species' own *_GROWTH_CHANCE_PCT when a
	// same-species neighbor is found within SPREAD_RADIUS_MM. Scaling
	// all three species by the same multiplier preserves their existing
	// 1:3:2 relative ratio as the spread weight, rather than introducing
	// a second, disconnected competition parameter - each species still
	// rolls independently, same as the spontaneous case, just at a
	// higher chance when it has something nearby to spread from.
	static float SPREAD_CHANCE_MULTIPLIER;

	// New departure from ELF's literal getCellColor() (2026-09-24, per
	// explicit user request) - NOT present in ELF, and NOT the same
	// mechanism as the white "negative space" tie rule in
	// heightMapShader.frag (that rule only ever covered an EXACT
	// all-zero tie; a cell with even one successful growth tick,
	// density=1/255, is not a tie and was never covered by it).
	//
	// The problem this fixes: the shader's winner-take-all color rule
	// has no fade - a channel that wins renders at FULL elevation-
	// modulated saturation regardless of how small its actual density
	// is, matching ELF's own getCellColor() exactly (see the header
	// note). For shrub (h,1,h) and fruit (1,h,h) that's fine even at low
	// elevation, since each has a channel pinned to 1.0 - but nut's
	// formula, (0,h,h), has NO pinned-bright channel, so a single lucky
	// nut growth tick at low elevationNorm renders that cell as solid
	// near-BLACK instantly, indistinguishable from a contour line or
	// real deep water to the eye, and scattered randomly wherever nut's
	// independent per-pixel growth happens to land first - confirmed
	// this is what the user was actually seeing (not contour lines,
	// which they'd already ruled out, and not deep water, which only
	// explains the one confirmed water body, not scattered patches
	// throughout otherwise-unrelated land).
	//
	// The fix: nut specifically doesn't win the comparison (falls
	// through to negative space, same as a true tie) until its density
	// clears this threshold - so a barely-nonzero nut cell reads as
	// still-blank land, same as the ties around it, rather than flashing
	// to black the instant a single tick succeeds. Shrub/fruit are
	// deliberately NOT touched - their instant full-saturation speckle
	// at low density was already confirmed correct/expected on real
	// hardware (see CLAUDE.md's 2026-09-22 pipeline-confirmation note),
	// and this only targets the specific formula (no pinned-bright
	// channel) that makes nut uniquely prone to reading as unwanted
	// black speckle instead of visible teal. Read directly by the
	// shader as a uniform - see SandSurfaceRenderer::drawSandbox().
	static float NUT_VISIBILITY_THRESHOLD;

	// Debug aid only - normally snow renders as flat white, identical to
	// un-grown negative-space land (see heightMapShader.frag's header
	// note), which makes it impossible to tell by eye whether a blank
	// area is "still growing" or "already snow." When true, the shader
	// tints snow a distinct pale blue instead, so the two are visually
	// separable while diagnosing calibration. SandSurfaceRenderer reads
	// this directly (it already includes VegetationField.h) to set the
	// shader uniform - see drawSandbox().
	static bool DEBUG_SHOW_SNOW;

private:
	// Shared by the eat*() helpers above - converts a kinect pixel
	// coordinate into grid indices, false if outside the grid.
	bool cellIndexAt(float kx, float ky, int & gx, int & gy) const;

	// elevation (mm) -> fraction of [elevationMin, elevationMax], clamped
	// 0..1 - the shared normalization every classification decision in
	// update()/isWaterAt()/isSnowAt() goes through. Mirrors
	// heightMapShader.frag's elevationNorm computation exactly, so
	// classification and cosmetic display agree on what "normalized
	// elevation" means.
	float normalizedElevation(float elevationMM) const;

	// True if a stochastic sample of nearby cells (within SPREAD_RADIUS_MM,
	// converted to grid cells via mmPerCell) finds an already-established
	// (density > 0) cell in the given species' density grid. Samples a
	// small fixed number of random points within the radius rather than
	// scanning it exhaustively - exhaustively scanning a real neighborhood
	// on a 500+-cell-wide native-resolution grid, for all three species,
	// every cell, every frame, would be a real per-frame cost; this is a
	// deliberate stochastic approximation instead, untested for
	// performance on real hardware - if it turns out too slow, the sample
	// count (see the .cpp) is the first thing to reduce.
	bool hasEstablishedNeighbor(int gx, int gy, cv::Mat const& density) const;

	std::shared_ptr<KinectProjector> kinectProjector;
	ofRectangle kinectROI;
	int step, cols, rows;
	bool gridReady;

	// Real-world mm spanned by one grid cell (one Kinect pixel, since
	// GRID_STEP==1) - cached once per grid (re)build in setKinectROI(),
	// via KinectProjector::kinectCoordToWorldCoord(), so SPREAD_RADIUS_MM
	// converts to a grid-cell radius that means the same real distance
	// regardless of a given installation's Kinect resolution.
	float mmPerCell;

	// Calibrated elevation range (mm) - see setElevationRange().
	float elevationMin, elevationMax;

	// Persistent 0..1 density per cell, CV_32F - see the header note on
	// the one-way growth rule.
	cv::Mat shrubDensity, fruitDensity, nutDensity;

	// Last frame's elevation per cell (mm) and whether it's populated yet -
	// see update()'s activity/TEMPERATURE computation.
	cv::Mat previousElevation;
	bool activityBaselineReady;
	float activityLevel;

	ofTexture combinedTex;
};
