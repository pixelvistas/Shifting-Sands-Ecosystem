/***********************************************************************
SonicParticle.h - a ring point for the sonic wave layer: damping, hand
and Tangible interaction, but deliberately no slope-tangential gravity
(unlike Critter). Every point in a ring starts with the same outward
speed, so with no terrain force to perturb it a ring expands as a clean
circle - gravity was tried first (matching Critter's physics) but with
32 independently-reacting points it read as the ring randomly tearing
apart on terrain noise rather than a coherent wave, so it was removed
rather than tuned down. Hands can still push/guide a ring, which reads
as deliberate interaction rather than noise. Each point owns a
SonicEngine voice for its lifetime. Height maps to volume, speed to
pitch, position to pan.

Ecosystem extension, part of the Shifting Sands fork of Magic Sand.
***********************************************************************/

#pragma once
#include "ofMain.h"

#include "../KinectProjector/KinectProjector.h"
#include "HandField.h"
#include "Tangible.h"
#include "SonicEngine.h"

class SonicParticle {
public:
	SonicParticle(std::shared_ptr<KinectProjector> const& k, ofPoint slocation, ofRectangle sborders, ofVec2f initialVelocity);

	// Allocates a voice. fixedLifetime > 0 overrides the usual randomized
	// LIFETIME_MIN..LIFETIME_MAX draw - ring points share one explicit
	// lifetime so the ring fades out together instead of unraveling as
	// individual points die on staggered random timers.
	void setup(SonicEngine & engine, float fixedLifetime = -1.0f);
	// Returns false once the particle's lifetime has expired - the caller
	// should then remove it (its voice is already released by then).
	bool update(HandField & handField, std::vector<Tangible> & tangibles, SonicEngine & engine);
	void draw();
	// Releases this particle's voice early - call before dropping a
	// particle for any reason other than update() returning false (which
	// already releases it itself), or the voice pool leaks a slot.
	void release(SonicEngine & engine);

	const ofPoint & getLocation() const { return location; }
	const ofVec2f & getVelocity() const { return velocity; }
	float getMass() const { return mass; }
	float getRadius() const { return radius; }

	void applyImpulse(const ofVec2f & impulse) { velocity += impulse / mass; }

	// Physics tuning.
	static float DAMPING;
	static float HAND_PUSH_STRENGTH;
	static float HERD_STRENGTH;

	// Lifetime, randomized per particle between these (seconds).
	static float LIFETIME_MIN;
	static float LIFETIME_MAX;

	// Sonification mapping.
	static float HEIGHT_MIN, HEIGHT_MAX; // elevationAtKinectCoord range -> volume
	static bool INVERT_HEIGHT;
	static float MAX_SPEED_FOR_PITCH; // speed at/above which pitch maxes out

private:
	void clampToBorders();

	std::shared_ptr<KinectProjector> kinectProjector;

	ofPoint location;
	ofVec2f velocity;
	ofVec2f acceleration;
	ofVec2f projectorCoord;

	float mass;
	float radius;

	float age;
	float lifetime;

	int voiceIndex;

	ofRectangle borders;
};
