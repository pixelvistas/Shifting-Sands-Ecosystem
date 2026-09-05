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
  sampling. This is a deliberate departure from the GRID_STEP==4
  convention MyceliumNetwork/HandField/PuckTracker share, since matching
  ELF's resolution mattered more here than matching that convention.
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

Same upload architecture as MyceliumNetwork: a CPU-side grid sampled from
elevationAtKinectCoord() each frame, uploaded as a single texture that
SandSurfaceRenderer's heightMapShader reads - see getTexture() /
getGridOrigin() / getGridStep(). The shader (not this class) is
responsible for turning R/G/B density into a color: it picks whichever
channel is strictly largest and colors the whole cell that plant's flat,
elevation-modulated color - see heightMapShader.frag - matching
BDlocation.getCellColor()'s winner-take-all comparison (ties, including
the initial all-zero state, render as ELF's Color.PINK fallback) rather
than blending all three proportionally. Density itself only decides the
comparison outcome here, never a fade amount - there is no partial/faded
color state in ELF, and now none in this shader either.

TEMPERATURE shifts both WATER_LEVEL_BASE and SNOW_LEVEL_BASE by the same
amount, matching ELF's temperature keys exactly (raising it both floods
more land and shrinks the snowcap, since the snow threshold rising means
fewer cells clear it) - see BDenvironment.stepCells()/incTemp()/decTemp().

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
	// cols/rows leaves current vegetation cover untouched, same convention
	// as MyceliumNetwork::setKinectROI().
	void setKinectROI(ofRectangle & KROI);
	void update();
	void drawGui();

	ofTexture & getTexture() { return combinedTex; }
	// Kinect-pixel-space origin and step size of the grid the texture
	// represents, for converting a shader's kinect pixel coordinate into
	// a texel lookup: (kinectCoord - getGridOrigin()) / getGridStep().
	ofVec2f getGridOrigin() const { return ofVec2f(kinectROI.x, kinectROI.y); }
	float getGridStep() const { return (float)step; }

	// Tunable in the debug GUI.
	static float TEMPERATURE;            // mm, shifts both water and snow lines together
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
	std::shared_ptr<KinectProjector> kinectProjector;
	ofRectangle kinectROI;
	int step, cols, rows;
	bool gridReady;

	// Persistent 0..1 density per cell, CV_32F - see the header note on
	// the one-way growth rule.
	cv::Mat shrubDensity, fruitDensity, nutDensity;

	ofTexture combinedTex;
};
