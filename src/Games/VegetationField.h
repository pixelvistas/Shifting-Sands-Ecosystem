/***********************************************************************
VegetationField.h - a per-cell flora simulation and its ELF-style visual
encoding: three plant "types" that grow and fade within elevation bands
relative to a tunable water/snow line, ported from the cellular growth
rules in Europe's Lost Frontiers' BDlocation/BDenvironment (see
ELFdev001/ELFDynamicSystem - the "ecosim model" this is patterned on) and
re-expressed as continuous per-cell density rather than ELF's per-tick
random growth, so it reads as smoothly thickening/thinning patches
instead of tick-by-tick flicker.

Same architecture as MyceliumNetwork: a CPU-side grid sampled from
elevationAtKinectCoord() each frame, uploaded as a single texture that
SandSurfaceRenderer's heightMapShader blends in - see getTexture() /
getGridOrigin() / getGridStep().

Texture encoding (RGBA, one texel per grid cell):
  R, G, B = density (0..1) of the three plant types (shrub/fruit/nut, in
            ELF's terms - see the *_MIN_ABOVE_WATER/*_MAX_BELOW_SNOW
            bands below), zero on water or snow cells.
  A       = category flag: ~1.0 water, ~0.5 snow, ~0.0 land - the shader
            uses this to pick a flat water/snow color instead of
            blending vegetation, mirroring BDlocation::getCellColor's
            water/snow/land branch.

Growth rule per cell, each tick: if the cell's current elevation falls
within a plant type's band, that channel's density rises toward 1 at
GROWTH_RATE; otherwise (including water/snow cells) it decays toward 0 at
DECAY_RATE. Reshaping the sand therefore visibly shifts vegetation cover
over a few seconds rather than snapping instantly, which is what makes
sculpting read as manipulating a living system instead of flipping a
lookup table.

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
	static float GROWTH_RATE;  // density gained per second while a cell is in-band
	static float DECAY_RATE;   // density lost per second while a cell is out-of-band (or water/snow)

private:
	std::shared_ptr<KinectProjector> kinectProjector;
	ofRectangle kinectROI;
	int step, cols, rows;
	bool gridReady;

	// Persistent 0..1 density per cell, CV_32F - see the header note on
	// the growth rule.
	cv::Mat shrubDensity, fruitDensity, nutDensity;

	ofTexture combinedTex;
};
