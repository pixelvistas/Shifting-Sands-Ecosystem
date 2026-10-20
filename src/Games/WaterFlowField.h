/***********************************************************************
WaterFlowField.h - a particle-trail ("runoff") visualization of how water
moves across the sculpted terrain, REPLACING the dense vector-arrow
version this file briefly was (see CLAUDE.md's 2026-10-19/20 entries).

Why the arrow version failed on hardware ("rapid blinking arrows"):
1. It was gated per frame on isWaterAt() AND on getFlowVelocityAt()
   clearing MIN_VELOCITY_TO_SHOW. stepFlow()'s water settles to a flat
   surface at rest, so its real velocity is ~0 almost everywhere - the
   only signal left near that threshold is Kinect depth noise, which
   flips a glyph on/off frame to frame.
2. isWaterAt() is VegetationField's water, which on this branch is a
   different elevation scale from what HeightColorMap.xml paints blue,
   so arrows appeared where nothing looked wet and vice versa.

This version follows the convention the reference systems actually use
(SND, Liu's flow imagery, fieldplay-style flow maps): many particles
advected through a SMOOTH, persistent vector field, each leaving a
fading trail, each with a finite lifespan that fades in and out (so
nothing ever pops on/off). The field is the terrain's own downhill
gradient - gravity-driven runoff, the direction water on the sand would
actually travel - computed from VegetationField::getElevationGrid()
(this frame's samples, no extra Kinect reads), downsampled, spatially
blurred, and exponentially smoothed in time so sensor noise cannot make
it jitter. Particles spawn only on visibly blue water
(VISIBLE_WATER_LINE_MM), run downhill with momentum and friction, and
fade out where they meet the shore, so nothing is drawn on land.

Known limitation (2026-10-20): inside standing water the bed gradient
only says where the lake is deepest, so particles drift toward the
bottom of each basin rather than showing a current. Real one-way
channel flow needs a source and a sink in the hydrology model itself -
see CLAUDE.md's 2026-10-20 hydrology entry.

Purely visual: reads VegetationField, never writes it, no effect on the
simulation. Same setup()/setProjectorRes()/setKinectROI()/update()/
draw*()/drawGui() shape as CCritterController. Trails are drawn as a
VBO mesh directly in projector space (no FBO - an alpha-blended FBO
over a transparent clear squares alpha and washes faint trails out).

Cost: field grid is cols/FIELD_DOWNSAMPLE x rows/FIELD_DOWNSAMPLE (~130x74
at the defaults - one resize + one blur per frame); particles are one
bilinear lookup + one kinectCoordToProjCoord() each per frame, and the
trails are drawn as a single VBO mesh. No per-cell work at volume.

Ecosystem extension, part of the Shifting Sands fork of Magic Sand.
***********************************************************************/

#pragma once
#include "ofMain.h"
#include <array>

#include "../KinectProjector/KinectProjector.h"
#include "VegetationField.h"

class WaterFlowField
{
public:
	// vegetationField is owned by ofApp and shared here as a query-only
	// pointer - same ownership convention as CCritterController::setup().
	void setup(std::shared_ptr<KinectProjector> const& k, VegetationField* vegetationField);
	void setProjectorRes(ofVec2f & PR);
	void setKinectROI(ofRectangle & KROI);

	void update();
	void drawMainWindow(float x, float y, float width, float height);
	void drawProjectorWindow();
	void drawGui();

	// Runtime kill switch, same A/B rationale as ENABLE_SPREAD/ENABLE_FLOW.
	static bool ENABLE_FLOW_FIELD;
	// Particles live only below this elevation (mm, the same scale as
	// HeightColorMap.xml's key heights - its teal "Shallow Water" key at
	// -3mm meets "Lowland Grass" at 0mm, so -1.5mm is where the projected
	// colour visibly turns from water to land). Spawn only there; a
	// particle that runs up onto land stops at the shore and fades out.
	// Interim mask: it follows the visible colour map, not stepFlow()'s
	// simulated depth - see CLAUDE.md's 2026-10-20 hydrology entry.
	static float VISIBLE_WATER_LINE_MM;
	// Where particles live and what moves them (2026-10-20). SIMULATED
	// uses VegetationField's virtual-pipe water: particles only on cells
	// it classifies as water, carried by its computed velocity - so the
	// particles show the hydrology model itself. COLOUR_MAP / BED_SLOPE
	// are the earlier behaviour (visible blue below VISIBLE_WATER_LINE_MM,
	// rolling down the smoothed bed gradient).
	enum { MASK_COLOUR_MAP = 0, MASK_SIMULATED = 1 };
	enum { MOTION_BED_SLOPE = 0, MOTION_SIMULATED = 1 };
	static int MASK_MODE;
	static int MOTION_MODE;
	// Multiplier on the simulated velocity (visual only), for when real
	// flow is too slow or fast to read.
	static float SIM_VELOCITY_SCALE;
	static int PARTICLE_COUNT;
	// Field resolution: grid cells per field sample. Larger = smoother,
	// broader flow; smaller = follows finer sculpted detail.
	static int FIELD_DOWNSAMPLE;
	// Spatial blur (in field samples) and temporal smoothing time
	// constant (seconds) applied to the elevation before differentiating -
	// these are what stop Kinect noise from making the flow jitter.
	static float FIELD_BLUR_SIGMA;
	static float FIELD_SMOOTHING_SEC;
	// Particle dynamics, in grid cells and seconds: acceleration per mm/cell
	// of slope, velocity decay rate (1/s), and a speed cap. Terminal speed
	// on a slope s (mm/cell) is roughly GRAVITY * s / FRICTION.
	static float GRAVITY;
	static float FRICTION;
	static float MAX_SPEED;
	// Lifespan range (s) - each particle fades in, lives, fades out, and
	// respawns at a random location.
	static float LIFE_MIN_SEC;
	static float LIFE_MAX_SEC;
	// Trail length in frames (capped at MAX_TRAIL) and line width (px).
	static int TRAIL_LENGTH;
	static float TRAIL_THICKNESS;
	// Particles brighten with speed; pooled/stalled particles dim to this
	// fraction so fast runoff channels stand out over still basins.
	static float STILL_BRIGHTNESS;
	static float PARTICLE_COLOR[3];

private:
	static const int MAX_TRAIL = 48;

	struct Particle {
		ofVec2f pos;   // grid coordinates (see VegetationField::getCols())
		ofVec2f vel;   // grid cells / second
		float age;
		float life;
		// Ring buffer of past projector-space positions, newest at head.
		std::array<ofVec2f, MAX_TRAIL> trail;
		int trailHead;
		int trailCount;
		bool active; // false = parked, no water anywhere to spawn into yet
	};

	void rebuildField(float dt);
	float sampleField(const cv::Mat & m, float gx, float gy) const; // bilinear, grid coords
	ofVec2f sampleGradient(float gx, float gy) const; // mm per grid cell, bilinear
	bool isVisibleWater(float gx, float gy) const;
	void respawn(Particle & p);
	ofPoint gridToProjCoord(float gx, float gy) const;

	std::shared_ptr<KinectProjector> kinectProjector;
	VegetationField* vegetationField; // query-only, owned by ofApp - see setup()
	ofRectangle kinectROI;
	ofVec2f projRes;

	// Smoothed, downsampled elevation and its gradient (field samples).
	cv::Mat smoothElevation, fieldElevation, gradX, gradY;
	// Field-sample indices currently below VISIBLE_WATER_LINE_MM, rebuilt
	// each frame - respawning picks from this list, so it always succeeds
	// when any water exists, however small a fraction of the table it is.
	std::vector<int> wetSamples;
	bool fieldReady = false;

	std::vector<Particle> particles;
	ofVboMesh trailMesh;
};
