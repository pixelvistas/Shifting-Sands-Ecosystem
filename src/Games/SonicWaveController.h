/***********************************************************************
SonicWaveController.h - Sand Noise Device-inspired sonic layer: while
the physical puck (see PuckTracker.h) is detected on the sand, it acts
as a point source for a ring of SonicParticles - evenly spaced points
around a circle, all pushed outward together (see SonicParticle.h) - that
expands once and then holds in place, representing the puck's current
resting spot rather than pulsing repeatedly. A new ring only replaces the
current one once the puck has moved more than ringMoveThreshold away, or
is retired (faded out - see SonicParticle::retire()) if the puck is
picked up and not put back down anywhere. The ring is rendered as a
single glowing closed loop, not as scattered dots.

Sonification (SonicEngine/SonicParticle's voice-per-point wiring) is
still present underneath but not the current focus - this pass is
graphics and physics only, per direction.

Deliberately independent of CCritterController/Critter - this is an
exploration inspired by Jay Van Dyke's Sand Noise Device, not an
extension of the ecosystem's moisture layer, and keeping it separate
means it can't destabilise the Critter system while it's being worked
out.

Tangible collision in SonicParticle currently has nothing to collide
with here (an empty vector is passed every frame) - whether tangibles
should be a single registry shared across particle systems is an open
question left for later rather than wired up under time pressure.

Ecosystem extension, part of the Shifting Sands fork of Magic Sand.
***********************************************************************/

#pragma once
#include "ofMain.h"

#include "../KinectProjector/KinectProjector.h"
#include "SonicParticle.h"
#include "SonicEngine.h"
#include "PuckTracker.h"
#include "Tangible.h"

// One expanding ring: a set of points spawned together around a circle,
// updated and drawn as a group. Owns nothing beyond its own points, so a
// ring is just removed once every point in it has died.
struct SonicRing {
	std::vector<SonicParticle> points;
};

class CSonicWaveController
{
public:
	// tracker is owned by ofApp and shared with CCritterController - see
	// the note in CritterController.h.
	void setup(std::shared_ptr<KinectProjector> const& k, PuckTracker* tracker);
	void setProjectorRes(ofVec2f & PR);
	void setKinectRes(ofVec2f & KR);
	void setKinectROI(ofRectangle & KROI);
	void exit();

	void update();
	void drawMainWindow(float x, float y, float width, float height);
	void drawProjectorWindow();
	void drawGui();

private:
	void spawnRingAt(const ofPoint & origin);
	void retireRing(SonicRing & ring); // starts its fade-out via SonicParticle::retire()
	void drawRing(const SonicRing & ring);

	std::shared_ptr<KinectProjector> kinectProjector;
	ofRectangle kinectROI;
	ofRectangle playArea; // kinectROI inset with a margin, same convention as CCritterController
	ofVec2f projRes, kinectRes;

	SonicEngine engine;
	PuckTracker* puckTracker;
	std::vector<SonicRing> rings;
	std::vector<Tangible> noTangibles; // always empty - see header note

	ofFbo fbo;

	bool hasActiveRing;        // whether "rings.back()" (if non-empty) is the current, non-retiring ring
	ofPoint activeRingOrigin;  // puck location the active ring was spawned at
	bool showPuckDebug;

	// Tunable in the debug GUI.
	float ringMoveThreshold; // kinect pixels the puck must move before its ring is replaced
	int ringPointCount;      // points evenly spaced around each ring
	float wavePushSpeed;     // initial outward speed of every point
	ofColor ringColor;
	float ringColorRGB[3];  // 0..1 mirror of ringColor, kept in sync - ImGui::ColorEdit3 needs float components, ofColor is unsigned char
};
