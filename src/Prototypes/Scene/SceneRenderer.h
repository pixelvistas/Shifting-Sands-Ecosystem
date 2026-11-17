/***********************************************************************
SceneRenderer - Prototype 4 ("Scene") display module.

A continuously animated, lit top-down scene of the sandbox (terrain light
and shade, swaying grass, drifting dappled light, the simulated water
with ripples and glints) passed through a screen-space stylise filter
(ASCII glyphs and/or an ordered colour dither). All motion comes from the
scene as smooth functions of time or of the ripple simulation; the filter
only decides how each pixel or cell is drawn.

Passes (shaders in bin/data/shaders/scene/GL2|GL3):
  0. SceneHeightInput: display height (EMA + Gaussian of the elevation
     grid, or the mock terrain) and the simulated water, fresh.
     RippleField: damped wave equation on the water.
  1. sceneRender: the lit scene in grid space, SCENE_MULT x the grid,
     RGBA16F.
  2. sceneWarp: the scene through the projector calibration (the grid
     mesh at kinectCoordToProjCoord every LUT_STEP cells, as Prototype 3)
     into a projector-sized RGBA16F FBO.
  3. SceneStylizer: Off / ASCII / Dither / Both, in projector pixels.

Display only: reads VegetationField (elevation grid, water depth and
velocity, elevation range, mm per cell) through const accessors; never
writes any simulation. ofApp updates it only while "Scene" is the display
mode, and no GL work happens before that.

Preset "Prototype 4: Scene": bin/data/settings/scenePreset.xml (Save/Load
in the Scene panel; loaded at start-up when present).
***********************************************************************/

#pragma once
#include "ofMain.h"
#include "SceneGL.h"
#include "SceneHeightInput.h"
#include "RippleField.h"
#include "SceneStylizer.h"

class KinectProjector;
class VegetationField;

class SceneRenderer
{
public:
	void setup(std::shared_ptr<KinectProjector> const & kp, const VegetationField * vf);
	void setProjectorRes(const ofVec2f & res);
	void update();
	void drawMainWindow(float x, float y, float w, float h);
	void drawProjectorWindow();
	void drawGui();
	bool savePreset();
	bool loadPreset();

	// Scene look (sceneRender.frag); defaults in the .cpp.
	static int SCENE_MULT;              // scene texels per grid cell (1..6)
	static bool FREEZE;                 // stops wind, dapple drift and drops
	static float HEIGHT_LO_MM, HEIGHT_HI_MM, TERRAIN_MID_POS;
	static ofFloatColor TERRAIN_LOW, TERRAIN_MID, TERRAIN_HIGH;
	static float EXAGGERATION;
	static float SUN_AZIMUTH_DEG, SUN_ELEVATION_DEG;
	static ofFloatColor SUN_COLOR;
	static float AMBIENT;
	static float DAPPLE_SCALE_MM, DAPPLED_LIGHT_SPEED, DAPPLE_DIRECTION_DEG, DAPPLE_CONTRAST, DAPPLE_SHADE;
	static float BLADE_LENGTH_MM, BLADE_WIDTH_MM, BLADES_PER_TUFT;
	static float GRASS_MIN_MM, GRASS_MAX_MM, GRASS_MAX_SLOPE, CLUMP_SCALE_MM, CLUMP_AMOUNT;
	static ofFloatColor GRASS_BASE, GRASS_TIP;
	static float TIP_BOOST;
	static float WIND_DIRECTION_DEG, WIND_STRENGTH, WIND_WAVELENGTH_MM, WIND_SPEED, GUST_STRENGTH, LEAN_SPREAD_DEG;
	static float WATER_EDGE_MM, WATER_DEEP_MM;
	static ofFloatColor WATER_SHALLOW, WATER_DEEP;
	static float RIPPLE_NORMAL_GAIN, SPEC_STRENGTH, SHININESS, RIPPLE_SHIMMER, HIGHLIGHT_MAX;
	static float REFLECT_STRENGTH, REFLECT_DIRECTION_DEG, REFLECT_DISTANCE_MM, REFLECT_AMP_MM, REFLECT_FREQ, REFLECT_SPEED;
	static float BLACK_POINT, BLACK_SOFTNESS;

private:
	void measureScale();
	void rebuildMesh();
	void renderScene();
	void warpScene();

	std::shared_ptr<KinectProjector> kinectProjector;
	const VegetationField * vegetation = nullptr;
	SceneHeightInput input;
	RippleField ripples;
	SceneStylizer stylizer;
	ofShader sceneShader, warpShader;
	bool glReady = false, glFailed = false;
	ofVec2f projRes;
	ofFbo sceneFbo, warpedFbo;
	ofMesh sceneQuad;
	ofVboMesh mesh;
	int meshCols = 0, meshRows = 0, meshMult = 0;
	int sceneCols = 0, sceneRows = 0;
	uint64_t lastUpdateFrame = 0, frameCounter = 0;
	float animTime = 0.0f;
	bool scaleMeasured = false;
	float pxPerMM = 0.0f;
	ofVec2f measuredOrigin;
	int measuredCols = 0, measuredRows = 0;
	SceneGL::GpuTimer tInput, tRipple, tScene, tWarp, tStyle;
	float cpuInputMs = 0.0f, cpuMeshMs = 0.0f;
	std::string presetStatus;
	char rampBuf[SceneStylizer::MAX_RAMP * 2 + 1] = {};
	bool hasFrame = false;
};
