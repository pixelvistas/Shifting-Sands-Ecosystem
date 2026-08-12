#include "Tangible.h"

float Tangible::MASS = 60.0f;
float Tangible::RADIUS = 16.0f;
float Tangible::DAMPING = 0.90f;
float Tangible::HAND_PUSH_STRENGTH = 3.0f;

Tangible::Tangible(std::shared_ptr<KinectProjector> const& k, ofPoint slocation, ofRectangle sborders)
{
	kinectProjector = k;
	location = slocation;
	velocity = ofVec2f(0);
	mass = MASS;
	radius = RADIUS;
	borders = sborders;
}

void Tangible::applyImpulse(const ofVec2f & impulse)
{
	velocity += impulse / mass;
}

void Tangible::update(HandField & handField)
{
	if (dragging)
		return;

	if (handField.isInHand(location.x, location.y)) {
		ofVec2f push = handField.pushDirection(location.x, location.y);
		if (push.lengthSquared() > 0)
			applyImpulse(push.getNormalized() * HAND_PUSH_STRENGTH); // divided by mass in applyImpulse - heavier pucks are harder to shove
	}

	velocity *= DAMPING;
	location += velocity;

	if (location.x < borders.getLeft())   { location.x = borders.getLeft();   velocity.x = 0; }
	if (location.x > borders.getRight())  { location.x = borders.getRight();  velocity.x = 0; }
	if (location.y < borders.getTop())    { location.y = borders.getTop();    velocity.y = 0; }
	if (location.y > borders.getBottom()) { location.y = borders.getBottom(); velocity.y = 0; }
}

void Tangible::draw()
{
	ofVec2f projectorCoord = kinectProjector->kinectCoordToProjCoord(location.x, location.y);

	ofPushStyle();
	ofPushMatrix();
	ofTranslate(projectorCoord);

	ofSetColor(255, 210, 60);
	ofFill();
	ofDrawCircle(0, 0, radius);
	ofNoFill();
	ofSetColor(40, 30, 0);
	ofSetLineWidth(2.0);
	ofDrawCircle(0, 0, radius);

	ofPopMatrix();
	ofPopStyle();
}
