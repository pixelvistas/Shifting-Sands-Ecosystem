/***********************************************************************
SonicWaveController.h - Sand Noise Device-inspired sonic layer: while
the physical puck (see PuckTracker.h) is detected on the sand, it acts
as a point source, periodically releasing a ring of SonicParticles -
evenly spaced points around a circle, all pushed outward together - that
then individually react to the terrain under their own physics (see
SonicParticle.h) until the ring's shared lifetime runs out. The ring is
rendered as a single glowing closed loop, not as scattered dots, so it
visibly ripples outward and deforms as its points cross slopes, pits,
and hands at different rates. No puck, no waves.

Sonification (SonicEngine/SonicParticle's voice-per-point wiring) is
still present underneath but not the current focus - this pass is
graphics and physics only, per direction.

Deliberately independent of CCritterController/Critter - this is an
exploration inspired by Jay Van Dyke's Sand Noise Device, not an
extension of the ecosystem's moisture layer, and keeping it separate
means it can't destabilise the Critter system while it's being worked
out. The two HandFields duplicate some per-frame computation (cheap at
this scale) but share tuning automatically, since HandField's tunables
are static - see HandField.h.

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
#include "HandField.h"
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
	void drawRing(const SonicRing & ring);

	std::shared_ptr<KinectProjector> kinectProjector;
	ofRectangle kinectROI;
	ofRectangle playArea; // kinectROI inset with a margin, same convention as CCritterController
	ofVec2f projRes, kinectRes;

	SonicEngine engine;
	HandField handField;
	PuckTracker* puckTracker;
	std::vector<SonicRing> rings;
	std::vector<Tangible> noTangibles; // always empty - see header note

	ofFbo fbo;

	float waveSpawnAccum; // seconds since the last ring while the puck has been present
	bool showPuckDebug;

	// Tunable in the debug GUI.
	float spawnInterval;    // seconds between rings while the puck is present
	int ringPointCount;     // points evenly spaced around each ring
	float wavePushSpeed;    // initial outward speed of every point
	ofColor ringColor;
	float ringColorRGB[3];  // 0..1 mirror of ringColor, kept in sync - ImGui::ColorEdit3 needs float components, ofColor is unsigned char
};
