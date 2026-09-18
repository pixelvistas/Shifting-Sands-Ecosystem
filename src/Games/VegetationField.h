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

LIVING_RANGE_FRACTION is the one deliberate departure from ELF's exact
ratio: ELF's literal LIVINGRANGE=200/255 (~0.78) consumes so much of
the range that water/snow are left with almost no margin (~11% each)
within any realistically-sized calibrated range - confirmed on real
hardware, not just worked out on paper: with this ratio, a hand-built
mound never reached the snowline and a hand-dug pit never reached the
waterline, regardless of the calibrated ceiling used. Loosened to 0.5 -
still clearly the dominant fraction (most of the range is livable land,
matching ELF's intent), but leaves 25% margin on each end so ordinary
sculpting can actually reach both lines.

Loosening LIVING_RANGE_FRACTION is exactly why SHRUB/FRUIT/NUT_LINE
have to be ratios of it rather than fixed fractions of the total range:
fruit's band width is livingRange - 2*fruitLineOffset. At ELF's own
numbers that's 0.78 - 2*0.235 ~= 31% of the range - plenty. Holding
FRUITLINE fixed at 0.235 of the *total* range while shrinking living
range to 0.5 crushes that band to 0.5 - 2*0.235 = 0.03 - about 3% of
the range, a near-hairline a real pixel rarely lands in - confirmed on
real hardware as "little to no red [fruit] ever appears." Deriving
FRUIT_LINE_FRACTION = FRUIT_LINE_RATIO * LIVING_RANGE_FRACTION each
frame instead keeps fruit's band at the same ~31%-of-living-range width
ELF's own ratio implies, however LIVING_RANGE_FRACTION itself is tuned.

TEMPERATURE/BASE_TEMPERATURE/MAX_TEMPERATURE_OFFSET are themselves now
fractions of the same calibrated range (0..1-ish, not millimeters) for
the same reason. BASE_TEMPERATURE's starting value is NOT ELF's literal
default (temperature=200/255) either, and not just because
LIVING_RANGE_FRACTION changed: even at ELF's own ratio, that value
places the water line exactly at the calibrated floor with zero margin,
which in ELF's own source is only usable because an operator manually
lowers temperature via decTemp() ('a') before/during a session to open
a reachable water band for their specific installation. This port has
no equivalent manual dial (TEMPERATURE only rises from BASE_TEMPERATURE
with activity, per the note below), so BASE_TEMPERATURE needs that
one-time manual placement instead, done live against the real box: the
Vegetation GUI panel's Temperature readout plus watching what a real dig
and a real mound actually do is the fastest way to place it, the same
way an ELF operator would have dialed 'q'/'a' once at setup time.

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

	// BDenvironment's LIVINGRANGE/255 - see the header note on why
	// LIVING_RANGE_FRACTION deliberately departs from ELF's literal ratio.
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
	// the header note.
	static float SHRUB_GROWTH_CHANCE_PCT;
	static float FRUIT_GROWTH_CHANCE_PCT;
	static float NUT_GROWTH_CHANCE_PCT;

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

	std::shared_ptr<KinectProjector> kinectProjector;
	ofRectangle kinectROI;
	int step, cols, rows;
	bool gridReady;

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
