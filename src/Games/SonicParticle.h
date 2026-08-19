/***********************************************************************
SonicParticle.h - a ring point for the sonic wave layer: damping and
Tangible interaction, but deliberately no slope-tangential gravity
(unlike Critter) and no hand interaction. Every point in a ring starts
with the same outward speed, so with no terrain force to perturb it a
ring expands as a clean circle - gravity was tried first (matching
Critter's physics) but with 32 independently-reacting points it read as
the ring randomly tearing apart on terrain noise rather than a coherent
wave, so it was removed rather than tuned down. Hand interaction was
tried too, but a ring is meant to sit still and represent the puck's
current resting spot - and the hand that just placed or is about to move
the puck is unavoidably right on top of it, so any hand force reads as
the ring "distorting" rather than as deliberate play. Each point owns a
SonicEngine voice for its lifetime, which the ring's controller extends
indefinitely (see retire()) rather than letting it expire on a fixed
timer - the ring is meant to persist until the puck moves, not pulse.

Ring growth is capped by distance from the ring's own origin
(maxRingRadius), not by clamping each point's x/y independently against
the play area rectangle - that per-axis clamp is what made a ring flatten
into a square near a corner, since points that would have crossed the
corner diagonally got pulled onto the two straight edges instead. A
radial cap keeps the ring a circle right up to the boundary regardless of
which direction the puck is nearest to it.

Ecosystem extension, part of the Shifting Sands fork of Magic Sand.
***********************************************************************/

#pragma once
#include "ofMain.h"

#include "../KinectProjector/KinectProjector.h"
#include "Tangible.h"
#include "SonicEngine.h"

class SonicParticle {
public:
	// sborders is used only for the pan (left-right/top-bottom) mapping,
	// not for clamping position - see maxRingRadius for that. The ring's
	// origin is slocation itself, since every point in a ring starts at
	// the same spot.
	SonicParticle(std::shared_ptr<KinectProjector> const& k, ofPoint slocation, ofRectangle sborders, float maxRingRadius, ofVec2f initialVelocity);

	// Allocates a voice. fixedLifetime > 0 overrides the usual randomized
	// LIFETIME_MIN..LIFETIME_MAX draw - ring points share one explicit
	// lifetime so the ring fades out together instead of unraveling as
	// individual points die on staggered random timers.
	void setup(SonicEngine & engine, float fixedLifetime = -1.0f);
	// Returns false once the particle's lifetime has expired - the caller
	// should then remove it (its voice is already released by then).
	bool update(std::vector<Tangible> & tangibles, SonicEngine & engine);
	void draw();
	// Releases this particle's voice early - call before dropping a
	// particle for any reason other than update() returning false (which
	// already releases it itself), or the voice pool leaks a slot.
	void release(SonicEngine & engine);
	// Schedules death fadeSeconds from now (pulling the deadline in if it
	// was already closer than that) rather than killing it outright, so a
	// retired ring still fades out through the normal end-of-life path
	// instead of popping. Call when the puck this ring belongs to has
	// moved on or disappeared.
	void retire(float fadeSeconds);

	const ofPoint & getLocation() const { return location; }
	const ofVec2f & getVelocity() const { return velocity; }
	float getMass() const { return mass; }
	float getRadius() const { return radius; }

	void applyImpulse(const ofVec2f & impulse) { velocity += impulse / mass; }

	// Physics tuning.
	static float DAMPING;

	// Lifetime, randomized per particle between these (seconds) when no
	// fixedLifetime is given to setup().
	static float LIFETIME_MIN;
	static float LIFETIME_MAX;

	// Sonification mapping.
	static float HEIGHT_MIN, HEIGHT_MAX; // elevationAtKinectCoord range -> volume
	static bool INVERT_HEIGHT;
	static float MAX_SPEED_FOR_PITCH; // speed at/above which pitch maxes out

private:
	void clampToRingRadius();

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

	ofRectangle borders; // pan mapping only
	ofPoint ringOrigin;
	float maxRingRadius;
};
