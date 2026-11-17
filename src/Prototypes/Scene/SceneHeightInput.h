/***********************************************************************
SceneHeightInput - Prototype 4 ("Scene"), pass 0: the inputs, at grid
resolution (one texel per VegetationField grid cell).

Display height: the raw height (VegetationField::getElevationGrid, or the
mock terrain) smoothed for display only: a temporal EMA (SMOOTH_TIME_SEC)
then a separable Gaussian (SMOOTH_RADIUS_CELLS, sigma in cells); stability
comes from this clean input, not from held decisions. getHeight(): R
display height (mm), G its rate of change (mm/s, ripple source only).

Water: VegetationField::getWaterDepthAt (fraction of the Vegetation
panel's elevation range, x that range = mm), getFlowVelocityAt (grid
cells/s), read fresh every frame with no smoothing or hysteresis; A = the
depth's rate of change (mm/s) against last frame. getWater(): R depth
(mm), G/B velocity, A d(depth)/dt. Never placed by elevation, except in
mock-terrain mode, where the mock water fills below MOCK_WATER_LEVEL_MM.

Mock terrain (MOCK_TERRAIN, display only): sceneMock.frag draws a mound,
a ridge and a basin, optional per-frame noise (MOCK_NOISE_MM) and an
optional slow "hand" digging a moving dimple (MOCK_HAND).
***********************************************************************/

#pragma once
#include "ofMain.h"

class VegetationField;

class SceneHeightInput
{
public:
	bool setup();                         // GL; once the mode is shown
	void reset() { resetPending = true; }
	// animTime: the animation clock (frozen with "Freeze animation");
	// frame: a counter for the mock noise hash.
	void update(const VegetationField & vf, float dt, float animTime, uint64_t frame);
	ofTexture & getHeight() { return display[cur].getTexture(); }
	ofTexture & getWater() { return useMockWater ? mockWater.getTexture() : waterTex; }
	int getCols() const { return cols; }
	int getRows() const { return rows; }
	float getLastCpuMs() const { return lastCpuMs; }

	static bool MOCK_TERRAIN;
	static float MOCK_WATER_LEVEL_MM;
	static bool MOCK_FLAT;               // flat sand only (noise tests)
	static bool MOCK_HAND;
	static float MOCK_HAND_SPEED;        // turns per minute
	static float MOCK_NOISE_MM;          // +- per frame (tests)
	static float SMOOTH_RADIUS_CELLS;    // Gaussian sigma
	static float SMOOTH_TIME_SEC;        // EMA time constant
	static float WATER_THRESHOLD_MM;     // shared with the scene and ripples

private:
	void allocate(int c, int r);
	ofShader mockShader, emaShader, blurShader;
	bool loaded = false;
	int cols = 0, rows = 0;
	ofFloatPixels rawPix, waterPix;
	ofTexture rawTex, waterTex;
	std::vector<float> lastFinite, prevDepth;
	ofFbo mockRaw, mockWater;
	ofFbo ema[2], blurX, display[2];
	int cur = 0;
	bool resetPending = true;
	bool useMockWater = false;
	ofMesh gridQuad;
	float lastCpuMs = 0.0f;
};
