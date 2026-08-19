/***********************************************************************
Critter.h - insect-sized agent obeying slope-tangential gravity rather
than Reynolds steering. Forked from Vehicle (vehicle.h): keeps the
location/velocity/acceleration shape, the ofApp-side spawn/ROI pattern,
and the gradientAtKinectCoord()/elevationAtKinectCoord() queries, but
drops the desired-velocity/maxSpeed steering machinery entirely - that
architecture exists to hold speed constant regardless of terrain, which
is the opposite of what a rolling body needs.

Physics: a = GRAVITY * gradientAtKinectCoord(location), mass-independent
(free acceleration downhill, deceleration uphill from one line). Mass
only governs how hard a hand or a Tangible has to push to move a Critter.
Damping is what keeps a pit a trap rather than a critter oscillating in
a basin forever. GRADIENT_SIGN exists because the sign of
gradientAtKinectCoord relative to true "downhill" depends on Kinect
mount and calibration and is meant to be verified empirically (spawn
~20 critters on a slope and watch which way they go), not derived from
the transform chain.

A small wander force is added on top of gravity, smoothly turning via a
persistent per-critter heading rather than resampling a random direction
every frame (that resampling is exactly what produced a "spinning in
place" artifact at true peaks/valleys, where the real gradient is near
zero and sensor noise dominated the facing angle instead). Wander is
scaled down by local slope steepness, so it dominates on flat ground
(critters roam and actually encounter the terrain and the Tangible
instead of freezing at spawn) but fades out on a real slope or pit wall,
where gravity should win and trapping should still hold.

The physical puck (PuckTracker) is treated as a solid obstacle, not
terrain: it collides like a Tangible (see update()'s puck params) rather
than being climbable via gravity/wander. The Kinect genuinely sees it as
raised sand, so gradientAtKinectCoord() still reports a bump there, but
without a hard collision boundary a critter could wander onto its low-
slope top and get stuck the same way it would in a shallow pit - the
puck is a recognized object, not scenery, so it needs to physically block
critters rather than merely being sloped terrain they happen to roll off
of most of the time.

Hand interaction is motion-aware, not just contact-aware: while the hand
is moving, nearby critters get carried along in its direction of travel
(HandField::herdForce - "guide"), which also covers direct contact since
the field saturates to full strength right at the hand's own footprint.
Only once the hand holds still does direct contact fall back to the old
hard push-out (HandField::pushDirection), so a cupped, held hand still
traps rather than letting critters drift right through it. This is what
keeps ordinary sculpting - which is mostly hand motion - from reading as
the critters fleeing outward the moment your hand nears the sand.

Ecosystem extension, part of the Shifting Sands fork of Magic Sand.
***********************************************************************/

#pragma once
#include "ofMain.h"

#include "../KinectProjector/KinectProjector.h"
#include "HandField.h"
#include "Tangible.h"

class Critter {
public:
	Critter(std::shared_ptr<KinectProjector> const& k, ofPoint slocation, ofRectangle sborders);

	void setup();
	// puckPresent/puckLocation/puckRadius describe the real, physical puck
	// (see PuckTracker) as a solid, immovable obstacle in kinect-pixel
	// space - separate from the simulated tangibles vector, since the
	// puck's position is ground truth from sensing, not physics. Defaults
	// to "no puck" so existing call sites don't need to change.
	void update(HandField & handField, std::vector<Tangible> & tangibles,
		bool puckPresent = false, const ofPoint & puckLocation = ofPoint(), float puckRadius = 0.0f);
	void draw();

	const ofPoint & getLocation() const { return location; }
	const ofVec2f & getVelocity() const { return velocity; }
	float getMass() const { return mass; }
	float getRadius() const { return radius; }
	// Reporting only - a critter below SLEEP_SPEED for a while. Does not
	// gate movement; wander and real forces are always integrated.
	bool isAsleep() const { return asleep; }

	void applyImpulse(const ofVec2f & impulse) { velocity += impulse / mass; asleep = false; sleepFrames = 0; }

	static void setDrawFlipped(bool df) { DrawFlipped = df; }

	// Tunable in the controller's debug GUI - see the tuning-order note above.
	static float GRAVITY;
	static float DAMPING;
	static float SLEEP_SPEED;
	static int SLEEP_FRAME_THRESHOLD;
	static float GRADIENT_SIGN; // +1.0 or -1.0, flip while watching critters on a slope
	static float HAND_PUSH_STRENGTH; // hard push-out when a still hand sits directly on a critter
	static float HERD_STRENGTH; // carried along by a moving hand's direction of travel
	static float WANDER_STRENGTH;
	static float WANDER_TURN_RATE; // max heading change per frame, radians
	static float WANDER_SLOPE_FALLOFF; // higher = wander dies out faster as slope steepens

private:
	void clampToBorders();

	std::shared_ptr<KinectProjector> kinectProjector;

	ofPoint location;
	ofVec2f velocity;
	ofVec2f acceleration;
	ofVec2f projectorCoord;
	float angle;

	float mass;
	float radius;

	float wanderAngle;

	bool asleep;
	int sleepFrames;

	ofRectangle borders;

	static bool DrawFlipped;
};
