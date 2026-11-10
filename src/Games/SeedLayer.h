/***********************************************************************
SeedLayer - draws SeedParticles (display only) the way WaterFlowField draws
its flow particles: squares alpha-blended into an FBO that is cleared to
black and faded each frame, composited additively over the sand. Grid
coordinates go to the projector through KinectProjector's
kinectCoordToProjCoord on a coarse lookup grid (as WaterFlowField).

VegetationField owns one (site-feedback model only) and updates it after
each SuccessionModel step; ofApp draws it after the sand surface.

Ecosystem extension, part of the Shifting Sands fork of Magic Sand.
***********************************************************************/

#pragma once
#include "ofMain.h"
#include "SeedParticles.h"

class KinectProjector;

class SeedLayer
{
public:
	void setProjectorRes(const ofVec2f & res);
	// All arrays read only (see SeedParticles::update).
	void update(std::shared_ptr<KinectProjector> const & kp, const float * const cover[3], const float * soil,
		const unsigned char * water, int cols, int rows, float mmPerCell, float dt, int interleave,
		ofVec2f gridOrigin, float gridStep);
	void drawMainWindow(float x, float y, float w, float h);
	void drawProjectorWindow();
	void drawGui(); // inside the Vegetation panel

	static float SEED_SIZE_PX;    // square size in projector pixels
	static float SEED_FADE;       // FBO fade per frame (trail length)
	static float SEED_BRIGHTNESS;

private:
	bool drawnThisFrame() const;
	SeedParticles particles;
	ofVec2f projRes;
	ofFbo fbo;
	ofVboMesh mesh;
	static const int LUT_STEP = 4;
	int lutW = 0, lutH = 0;
	std::vector<ofVec2f> lut;
	ofVec2f toProj(float gx, float gy) const;
	uint64_t updatedFrame = 0;
};
