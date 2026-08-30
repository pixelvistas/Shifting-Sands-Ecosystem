#include "Critter.h"

float Critter::GRAVITY = 0.05f;
float Critter::DAMPING = 0.92f;
float Critter::SLEEP_SPEED = 0.05f;
int Critter::SLEEP_FRAME_THRESHOLD = 15;
float Critter::GRADIENT_SIGN = 1.0f;
float Critter::HAND_PUSH_STRENGTH = 2.0f;
float Critter::HERD_STRENGTH = 0.15f;
float Critter::WANDER_STRENGTH = 0.03f;
float Critter::WANDER_TURN_RATE = 0.3f;
float Critter::WANDER_SLOPE_FALLOFF = 10.0f;
ofColor Critter::BODY_COLOR = ofColor(150, 210, 255); // pale blue, per the ELF paper's "individual dots of pale blue represent deer"
ofColor Critter::IN_RING_COLOR = ofColor(60, 170, 255); // glowing neon blue
bool Critter::DrawFlipped = false;

Critter::Critter(std::shared_ptr<KinectProjector> const& k, ofPoint slocation, ofRectangle sborders)
{
	kinectProjector = k;
	location = slocation;
	borders = sborders;
	velocity = ofVec2f(0);
	acceleration = ofVec2f(0);
	angle = ofRandom(360);
	wanderAngle = ofRandom(TWO_PI);
	asleep = false;
	sleepFrames = 0;
	inRing = false;
}

void Critter::setup()
{
	radius = ofRandom(10.0f, 20.0f); // 5x the original insect-sized 2-4
	mass = ofRandom(0.5f, 2.0f);
}

void Critter::clampToBorders()
{
	if (location.x < borders.getLeft())   { location.x = borders.getLeft();   velocity.x = 0; }
	if (location.x > borders.getRight())  { location.x = borders.getRight();  velocity.x = 0; }
	if (location.y < borders.getTop())    { location.y = borders.getTop();    velocity.y = 0; }
	if (location.y > borders.getBottom()) { location.y = borders.getBottom(); velocity.y = 0; }
}

void Critter::update(HandField & handField, std::vector<Tangible> & tangibles,
	bool puckPresent, const ofPoint & puckLocation, float puckRadius, bool insideRing)
{
	inRing = insideRing;

	// Slope-tangential gravity: mass-independent by design, so mass is free
	// to mean something else - see the header note on the gravity/mass split.
	ofVec2f grad = kinectProjector->gradientAtKinectCoord(location.x, location.y);
	acceleration = grad * GRAVITY * GRADIENT_SIGN;

	// Smooth wander: heading drifts slowly rather than resampling a random
	// direction every frame, and fades out as the real slope steepens, so
	// flat ground gets exploration while a real pit or hillside still lets
	// gravity win and trap the critter.
	wanderAngle += ofRandom(-WANDER_TURN_RATE, WANDER_TURN_RATE);
	float wanderScale = 1.0f / (1.0f + grad.length() * WANDER_SLOPE_FALLOFF);
	acceleration += ofVec2f(cos(wanderAngle), sin(wanderAngle)) * WANDER_STRENGTH * wanderScale;

	// A moving hand guides nearby critters along with it (herdForce already
	// saturates to full strength right at the hand's own footprint, so this
	// covers direct contact too). Fed into acceleration rather than a raw
	// impulse so it goes through the same damped integration as gravity and
	// wander below - applying it as a fresh full-strength impulse every
	// single frame a critter stayed in range had nothing to damp between
	// additions and read as critters flinging themselves away. Only once
	// the hand is genuinely held still (isHandStill(), not just "velocity
	// isn't exactly zero") does direct contact fall back to the old hard
	// push-out, so a cupped, held hand still traps.
	if (!handField.isHandStill()) {
		ofVec2f herd = handField.herdForce(location.x, location.y);
		if (herd.lengthSquared() > 0)
			acceleration += herd * HERD_STRENGTH;
	} else if (handField.isInHand(location.x, location.y)) {
		ofVec2f push = handField.pushDirection(location.x, location.y);
		if (push.lengthSquared() > 0)
			applyImpulse(push.getNormalized() * HAND_PUSH_STRENGTH);
	}

	for (auto & t : tangibles) {
		ofVec2f delta = location - t.getLocation();
		float dist = delta.length();
		float minDist = radius + t.getRadius();
		if (dist > 0 && dist < minDist) {
			ofVec2f normal = delta / dist;
			float overlap = minDist - dist;
			float totalMass = mass + t.getMass();

			// De-penetration split by inverse mass: the lighter body (almost
			// always the critter) gives up most of the overlap.
			location += normal * overlap * (t.getMass() / totalMass);

			// Standard unequal-mass collision impulse along the contact normal.
			ofVec2f relativeVelocity = velocity - t.getVelocity();
			float vAlongNormal = relativeVelocity.dot(normal);
			if (vAlongNormal < 0) {
				const float restitution = 0.3f;
				float invMassSum = 1.0f / mass + 1.0f / t.getMass();
				float j = -(1.0f + restitution) * vAlongNormal / invMassSum;
				ofVec2f impulse = normal * j;
				applyImpulse(impulse);
				t.applyImpulse(-impulse);
			}
		}
	}

	if (puckPresent) {
		ofVec2f delta = location - puckLocation;
		float dist = delta.length();
		float minDist = radius + puckRadius;
		if (dist > 0 && dist < minDist) {
			ofVec2f normal = delta / dist;

			// The puck's position is sensed, not simulated - it doesn't
			// move in response to critters, so unlike the Tangible loop
			// above there's no reciprocal impulse and the critter absorbs
			// all of the de-penetration itself.
			location += normal * (minDist - dist);

			// Same restitution formula as the Tangible case in the limit
			// of infinite obstacle mass: reflect the velocity component
			// along the contact normal rather than computing an impulse/mass
			// split that would converge to this anyway.
			float vAlongNormal = velocity.dot(normal);
			if (vAlongNormal < 0) {
				const float restitution = 0.3f;
				velocity -= normal * ((1.0f + restitution) * vAlongNormal);
			}
		}
	}

	// Always integrate - wander guarantees flat ground never truly goes
	// still, and damping is what keeps a real pit from oscillating, so
	// there's no need to hard-freeze movement the way a "sleep" gate would.
	velocity += acceleration;
	velocity *= DAMPING;
	location += velocity;

	if (velocity.length() < SLEEP_SPEED) {
		sleepFrames++;
		asleep = sleepFrames > SLEEP_FRAME_THRESHOLD;
	} else {
		sleepFrames = 0;
		asleep = false;
	}

	clampToBorders();

	if (velocity.lengthSquared() > 0.0001f)
		angle = ofRadToDeg(atan2(velocity.y, velocity.x));

	projectorCoord = kinectProjector->kinectCoordToProjCoord(location.x, location.y);
}

void Critter::draw()
{
	ofPushMatrix();
	ofPushStyle();
	ofTranslate(projectorCoord);
	if (DrawFlipped)
		ofRotate(180 + angle);
	else
		ofRotate(angle);

	float len = radius * 2.0f;
	float wid = radius * 1.2f;

	if (inRing) {
		// Cheap glow halo behind the body, same additive-blend trick the
		// sonic ring itself uses (see SonicWaveController::drawRing).
		ofEnableBlendMode(OF_BLENDMODE_ADD);
		for (int pass = 3; pass >= 1; pass--) {
			ofSetColor(IN_RING_COLOR, 60);
			ofDrawCircle(0, 0, radius * (1.0f + pass * 0.5f));
		}
		ofDisableBlendMode();
		ofSetColor(IN_RING_COLOR);
	} else {
		ofSetColor(BODY_COLOR);
	}

	ofFill();
	ofDrawTriangle(len * 0.6f, 0, -len * 0.4f, -wid * 0.5f, -len * 0.4f, wid * 0.5f);
	ofNoFill();

	ofPopStyle();
	ofPopMatrix();
}
