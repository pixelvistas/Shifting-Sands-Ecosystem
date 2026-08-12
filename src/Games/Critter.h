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
Damping plus a sleep threshold is what actually makes a pit a trap -
without damping, a critter oscillates in a basin forever instead of
settling. GRADIENT_SIGN exists because the sign of gradientAtKinectCoord
relative to true "downhill" depends on Kinect mount and calibration and
is meant to be verified empirically (spawn ~20 critters on a slope and
watch which way they go), not derived from the transform chain.

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
	void update(HandField & handField, std::vector<Tangible> & tangibles);
	void draw();

	const ofPoint & getLocation() const { return location; }
	const ofVec2f & getVelocity() const { return velocity; }
	float getMass() const { return mass; }
	float getRadius() const { return radius; }
	bool isAsleep() const { return asleep; }

	void applyImpulse(const ofVec2f & impulse) { velocity += impulse / mass; asleep = false; sleepFrames = 0; }

	static void setDrawFlipped(bool df) { DrawFlipped = df; }

	// Tunable in the controller's debug GUI - see the tuning-order note above.
	static float GRAVITY;
	static float DAMPING;
	static float SLEEP_SPEED;
	static int SLEEP_FRAME_THRESHOLD;
	static float GRADIENT_SIGN; // +1.0 or -1.0, flip while watching critters on a slope
	static float HAND_PUSH_STRENGTH;

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

	bool asleep;
	int sleepFrames;

	ofRectangle borders;

	static bool DrawFlipped;
};
