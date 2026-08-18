/***********************************************************************
SonicWaveController.h - Sand Noise Device-inspired sonic layer: a wave
periodically sweeps across the play area, releasing bursts of
SonicParticles that then react to the terrain under their own physics
(see SonicParticle.h) until they age out. Owns the SonicEngine (audio
output) and its own HandField instance for hand interaction.

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
#include "Tangible.h"

class CSonicWaveController
{
public:
	void setup(std::shared_ptr<KinectProjector> const& k);
	void setProjectorRes(ofVec2f & PR);
	void setKinectRes(ofVec2f & KR);
	void setKinectROI(ofRectangle & KROI);
	void exit();

	void update();
	void drawMainWindow(float x, float y, float width, float height);
	void drawProjectorWindow();
	void drawGui();

private:
	void spawnWaveParticles();

	std::shared_ptr<KinectProjector> kinectProjector;
	ofRectangle kinectROI;
	ofRectangle playArea; // kinectROI inset with a margin, same convention as CCritterController
	ofVec2f projRes, kinectRes;

	SonicEngine engine;
	HandField handField;
	std::vector<SonicParticle> particles;
	std::vector<Tangible> noTangibles; // always empty - see header note

	ofFbo fbo;

	bool waveActive;
	float waveProgress;    // 0..1 across the current sweep
	float waveSpawnAccum;  // seconds since this sweep's last spawn burst
	float waveRestTimer;   // seconds since the last sweep finished

	// Tunable in the debug GUI.
	float waveDuration;     // seconds for one sweep to cross the play area
	float waveInterval;     // seconds of rest between sweeps
	float spawnInterval;    // seconds between spawn bursts during a sweep
	int particlesPerSpawn;
	float wavePushSpeed;    // initial nudge speed in the wave's direction of travel
};
