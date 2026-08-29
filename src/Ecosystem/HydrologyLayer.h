/***********************************************************************
HydrologyLayer.h - owns the flowing-water particle population: spawning
(from the physical puck, or the debug GUI), the per-frame update/cull,
and drawing into the projector and main windows. Structured identically
to CCritterController (same fbo-per-controller, same shared/query-only
PuckTracker pointer, same drawMainWindow()/drawProjectorWindow()/
drawGui() split) rather than introducing a new pattern for what is, at
this level, the same kind of thing: a persistent population of simple
physical agents reacting to the sand and the hand.

ECOSIMSPEC.md build order step 1 ("gradient map + particle advection +
point rendering, no ecology") with step 3 ("puck tracking as spawn
source") folded in - flow with nothing feeding it would be invisible on
the actual hardware, so there is no useful way to test step 1 alone.

Also owns a coarse moisture grid (§5.3's texWater.W, as a CPU cv::Mat
instead of a GPU texture - same "no GPU-compute precedent in this fork"
reasoning as the particles themselves): each alive particle deposits into
the cell under it every frame, and the whole grid decays (evaporates)
over time. This is the minimum slice of step 2 ("deposition and ET
death") VegetationLayer actually needs to read - not the full ET death
model (no field-capacity/Wcap, no per-particle death probability yet),
just "how wet has this cell been recently," which is what SI_moist reads.

Ecosystem module, part of the Shifting Sands fork of Magic Sand.
***********************************************************************/

#pragma once
#include "ofMain.h"
#include "ofxOpenCv.h"

#include "../KinectProjector/KinectProjector.h"
#include "../Games/HandField.h"
#include "../Games/PuckTracker.h"
#include "HydrologyParticle.h"

class HydrologyLayer {
public:
	// tracker is owned by ofApp and shared with the other ecosystem/game
	// layers - see CCritterController::setup()'s identical note on why
	// this is a query-only pointer rather than its own detector.
	void setup(std::shared_ptr<KinectProjector> const& k, PuckTracker* tracker);
	void setProjectorRes(ofVec2f & PR);
	void setKinectRes(ofVec2f & KR);
	void setKinectROI(ofRectangle & KROI);

	void update();
	void drawMainWindow(float x, float y, float width, float height);
	void drawProjectorWindow();
	void drawGui();

	int getParticleCount() const { return (int)particles.size(); }

	// 0..1, how wet cell (kinectX, kinectY) has been recently - see the
	// header note. Returns 0 if the grid isn't allocated yet or the point
	// falls outside kinectROI.
	float getMoistureAt(float kinectX, float kinectY) const;

	// Particles per second spawned while a puck is present - tunable in
	// the debug GUI. See ECOSIMSPEC.md §6 "spawn_rate".
	static float SPAWN_RATE;
	static int MAX_PARTICLES;

	// Moisture grid tuning - see ECOSIMSPEC.md §6 deposit_rate/wcap_bare
	// for the published starting point; not the same units (this is a
	// flat 0..1 accumulator, no field-capacity term yet).
	static float MOISTURE_DEPOSIT_RATE; // per second, added per particle occupying a cell
	static float MOISTURE_DECAY_RATE;   // per second, evaporation

private:
	void spawnAt(const ofPoint & loc, int n);
	void updateMoistureGrid(float dt);
	bool moistureGridCoordAt(float kinectX, float kinectY, int & gx, int & gy) const;

	std::shared_ptr<KinectProjector> kinectProjector;
	ofRectangle kinectROI;
	ofRectangle playArea; // same 5% margin convention as CritterController
	ofVec2f projRes, kinectRes;

	HandField handField;
	PuckTracker* puckTracker;
	std::vector<HydrologyParticle> particles;

	float spawnAccumulator; // fractional particles-per-frame carried over, so SPAWN_RATE isn't rounded down to 0 at low frame counts

	cv::Mat moisture; // CV_32F, 0..1 - see header note
	int moistureStep, moistureCols, moistureRows;

	ofFbo fbo;

	bool showHandDebug;
	bool showMoistureDebug;
	int manualSpawnCount; // tunable - how many the GUI "Add particles" button drops at once
};
