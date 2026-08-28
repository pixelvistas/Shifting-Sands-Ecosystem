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
Steps 2 (deposition/ET) onward are NOT implemented here; see the ECOSIM
build order for what's still ahead of this file.

Ecosystem module, part of the Shifting Sands fork of Magic Sand.
***********************************************************************/

#pragma once
#include "ofMain.h"

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

	// Particles per second spawned while a puck is present - tunable in
	// the debug GUI. See ECOSIMSPEC.md §6 "spawn_rate".
	static float SPAWN_RATE;
	static int MAX_PARTICLES;

private:
	void spawnAt(const ofPoint & loc, int n);

	std::shared_ptr<KinectProjector> kinectProjector;
	ofRectangle kinectROI;
	ofRectangle playArea; // same 5% margin convention as CritterController
	ofVec2f projRes, kinectRes;

	HandField handField;
	PuckTracker* puckTracker;
	std::vector<HydrologyParticle> particles;

	float spawnAccumulator; // fractional particles-per-frame carried over, so SPAWN_RATE isn't rounded down to 0 at low frame counts

	ofFbo fbo;

	bool showHandDebug;
	int manualSpawnCount; // tunable - how many the GUI "Add particles" button drops at once
};
