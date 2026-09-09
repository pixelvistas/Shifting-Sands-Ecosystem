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
- Per-species growth rates in a 1:3:2 ratio (SHRUB:FRUIT:NUT), matching
  BDlocation's SHRUBGROWTH=1/FRUITGROWTH=3/NUTGROWTH=2 per-tick growth
  chances - fruit is the fastest grower, shrub the slowest, so in the
  wide middle elevation band where all three are eligible, fruit tends
  to win the color out over time exactly as it does in ELF.
- One-way growth on land: a channel only ever rises (while its band
  condition holds) or holds steady (while out of band but still land);
  it never decays just from drifting out of a band. Only an actual
  transition to water or snow resets all three to 0 instantly - see
  BDlocation.makeSnow()/makeWater(), which zero shrubs/fruits/nuts
  outright with no easing, and stepCells(), whose growShrubs() etc.
  calls are the *only* per-tick modifications to those counts (nothing
  in ELF's source path ever decrements them for leaving a band while
  still land).

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
returns flat Color.PINK there, but the shader deliberately renders that
case as negative space instead, since ELF's own photographed sandbox
output shows bare land as plain sand, not a painted pink expanse - see
heightMapShader.frag's header note. Density itself only decides the
comparison outcome here, never a fade amount - there is no partial/faded
color state in ELF, and now none in this shader either.

TEMPERATURE shifts both WATER_LEVEL_BASE and SNOW_LEVEL_BASE by the same
amount, matching ELF's temperature effect exactly (raising it both floods
more land and shrinks the snowcap, since the snow threshold rising means
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

ACTIVITY_NOISE_FLOOR exists because "reacting to sensor noise" is not
just a jitter risk but a real failure mode on physical Kinect hardware:
depth measurement noise of a few mm per pixel is present on every cell
every frame, even when nothing is touching the sand - unlike genuine
sculpting, which is confined to whatever the hand is actually touching,
this noise is not diluted by averaging over the whole grid. Left
unfiltered it pins activityLevel just above zero permanently, which
saturates TEMPERATURE at MAX_TEMPERATURE_OFFSET on a perfectly still
sandbox, floods land that would otherwise hold its vegetation, and zeros
it via the water/snow branch above - visually, the whole play area
decaying to the tie-break PINK fallback over time with no participant
interaction at all. Per-cell deltas at or below this floor are dropped
from the activity sum entirely (not merely damped), so a still sandbox's
activityLevel reads as exactly 0 and TEMPERATURE eases back to
BASE_TEMPERATURE, matching ELF's undisturbed baseline.

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

	// Tunable in the debug GUI.
	static float TEMPERATURE;            // mm, shifts both water and snow lines together - computed each frame, see the header note; not a direct slider
	static float BASE_TEMPERATURE;       // mm, TEMPERATURE's resting value when the sand is undisturbed
	static float ACTIVITY_TO_TEMPERATURE; // mm of TEMPERATURE offset per mm of average per-cell elevation change
	static float MAX_TEMPERATURE_OFFSET; // mm, clamps how far activity alone can push TEMPERATURE above BASE_TEMPERATURE
	// mm, per-cell elevation change below which a frame's delta is treated as
	// depth-sensor noise rather than real sculpting and dropped from the
	// activity sum entirely - see update()'s header note on why this exists.
	static float ACTIVITY_NOISE_FLOOR;
	static float TEMPERATURE_EASE_RATE;  // how fast TEMPERATURE chases its activity-driven target, per second
	static float WATER_LEVEL_BASE;       // mm, elevation below which a cell is water at TEMPERATURE == 0
	static float SNOW_LEVEL_BASE;        // mm, elevation above which a cell is snow at TEMPERATURE == 0
	static float SHRUB_MIN_ABOVE_WATER;  // shrubs grow everywhere from this height up to the snow line
	static float FRUIT_MIN_ABOVE_WATER;
	static float FRUIT_MAX_BELOW_SNOW;
	static float NUT_MIN_ABOVE_WATER;
	static float NUT_MAX_BELOW_SNOW;
	// Density gained per second while a cell is in-band, 1:3:2 ratio matching
	// ELF's SHRUBGROWTH:FRUITGROWTH:NUTGROWTH per-tick chances.
	static float SHRUB_GROWTH_RATE;
	static float FRUIT_GROWTH_RATE;
	static float NUT_GROWTH_RATE;

private:
	// Shared by the eat*() helpers above - converts a kinect pixel
	// coordinate into grid indices, false if outside the grid.
	bool cellIndexAt(float kx, float ky, int & gx, int & gy) const;

	std::shared_ptr<KinectProjector> kinectProjector;
	ofRectangle kinectROI;
	int step, cols, rows;
	bool gridReady;

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
