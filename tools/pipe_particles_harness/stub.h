// Minimal stand-ins for the openFrameworks/OpenCV/VegetationField pieces
// that stepFlow() touches, so the real function compiles headless.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
extern double g_frameDt;
inline float ofGetLastFrameTime() { return (float)g_frameDt; }
inline uint64_t ofGetElapsedTimeMicros() { return 0; }
namespace cv {
struct Mat {
	std::vector<float> d; int r = 0, c = 0;
	template <class T> T * ptr(int) { return d.data(); }
	template <class T> const T * ptr(int) const { return d.data(); }
	template <class T> T & at(int y, int x) { return d[y * c + x]; }
	void init(int R, int C) { r = R; c = C; d.assign((size_t)R * C, 0.0f); }
};
}
struct VegetationField {
	enum { EDGE_LEFT = 0, EDGE_RIGHT = 1, EDGE_TOP = 2, EDGE_BOTTOM = 3 };
	static float FLOW_GRAVITY, FLOW_DAMPING_PER_SEC, FLOW_TERRAIN_SMOOTHING_SEC, SOURCE_SPAN, INFLOW_MM_PER_SEC,
		WATER_TABLE_MM, SEEPAGE_MM_PER_SEC, MIN_VISIBLE_WATER_DEPTH_FRAC, WATER_HYSTERESIS_FRAC;
	static int FLOW_SUBSTEPS, SOURCE_EDGE, SINK_EDGE;
	static bool USE_FLAT_SEA, ENABLE_WATER_TABLE;
	int rows = 0, cols = 0;
	float elevationMin = -142.6f, elevationMax = 142.6f; // the box's calibrated range
	cv::Mat previousElevation, flowTerrain, waterDepth, fluxL, fluxR, fluxT, fluxB, velX, velY;
	bool flowTerrainReady = false;
	float lastFlowMs = 0, lastWaterVolume = 0, lastMaxDepthMM = 0, lastInflowRate = 0, lastOutflowRate = 0, lastSeepageRate = 0;
	int lastWetCells = 0, lastCellsBelowTable = 0;
	float normalizedElevation(float e) const { return (e - elevationMin) / (elevationMax - elevationMin); }
	void init(int R, int C) {
		rows = R; cols = C;
		for (cv::Mat * m : { &previousElevation, &flowTerrain, &waterDepth, &fluxL, &fluxR, &fluxT, &fluxB, &velX, &velY })
			m->init(R, C);
	}
	void stepFlow(float waterLevelFrac);
	float addSourceInflow(float * depthMM, float dt);
	float addSeepage(float * depthMM, const float * terrMM, float dt);
};
