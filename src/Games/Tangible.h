/***********************************************************************
Tangible.h - a pushable virtual object standing in for a physical
tangible puck (spring / seed dispenser / disturbance marker etc.), until
LED tracking of real objects lands. Moved only by hand contact and by
collisions with Critters - it has no gravity of its own.

Ecosystem extension, part of the Shifting Sands fork of Magic Sand.
***********************************************************************/

#pragma once
#include "ofMain.h"

#include "../KinectProjector/KinectProjector.h"
#include "HandField.h"

class Tangible {
public:
	Tangible(std::shared_ptr<KinectProjector> const& k, ofPoint slocation, ofRectangle sborders);

	void update(HandField & handField);
	void draw();

	const ofPoint & getLocation() const { return location; }
	const ofVec2f & getVelocity() const { return velocity; }
	float getMass() const { return mass; }
	float getRadius() const { return radius; }

	void applyImpulse(const ofVec2f & impulse); // velocity += impulse / mass

	// Mouse-drag stand-in for real tangible tracking: while dragging, the
	// controller drives location directly and physics stands down.
	void setLocation(const ofPoint & loc) { location = loc; }
	void setDragging(bool d) { dragging = d; velocity = ofVec2f(0); }
	bool isDragging() const { return dragging; }

	static float MASS;
	static float RADIUS;
	static float DAMPING;
	static float HAND_PUSH_STRENGTH;

private:
	std::shared_ptr<KinectProjector> kinectProjector;

	ofPoint location;
	ofVec2f velocity;
	float mass;
	float radius;
	ofRectangle borders;
	bool dragging = false;
};
