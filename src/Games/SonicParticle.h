/***********************************************************************
SonicParticle.h - a Sand Noise Device-style agent: slope-tangential
gravity, damping, hand and Tangible interaction, same physics shape as
Critter (see Critter.h) - but no wander (SND's particles don't explore,
they're released by a wave and then just react to the terrain until
they die), and each one owns a SonicEngine voice for the lifetime of its
existence. Height maps to volume, speed to pitch, position to pan.

GRADIENT_SIGN is deliberately Critter::GRADIENT_SIGN, not a separate
constant - it's the same physical fact (which way is downhill for this
Kinect's mount and calibration) regardless of which particle population
is reading it, so there's exactly one place to verify and flip it.

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
	static float GRAVITY;
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
