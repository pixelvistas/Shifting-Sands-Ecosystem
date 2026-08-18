#include "SonicParticle.h"
#include "Critter.h" // for Critter::GRADIENT_SIGN - see header note

float SonicParticle::GRAVITY = 0.08f;
float SonicParticle::DAMPING = 0.94f;
float SonicParticle::HAND_PUSH_STRENGTH = 2.0f;
float SonicParticle::HERD_STRENGTH = 0.15f;
float SonicParticle::LIFETIME_MIN = 5.0f;
float SonicParticle::LIFETIME_MAX = 12.0f;
float SonicParticle::HEIGHT_MIN = -50.0f;
float SonicParticle::HEIGHT_MAX = 50.0f;
bool SonicParticle::INVERT_HEIGHT = false;
float SonicParticle::MAX_SPEED_FOR_PITCH = 3.0f;

SonicParticle::SonicParticle(std::shared_ptr<KinectProjector> const& k, ofPoint slocation, ofRectangle sborders, ofVec2f initialVelocity)
{
	kinectProjector = k;
	location = slocation;
	borders = sborders;
	velocity = initialVelocity;
	acceleration = ofVec2f(0);
	age = 0.0f;
	voiceIndex = -1;
}

void SonicParticle::setup(SonicEngine & engine)
{
	radius = ofRandom(2.0f, 4.0f);
	mass = ofRandom(0.5f, 2.0f);
	lifetime = ofRandom(LIFETIME_MIN, LIFETIME_MAX);
	voiceIndex = engine.allocateVoice();
}

void SonicParticle::clampToBorders()
{
	if (location.x < borders.getLeft())   { location.x = borders.getLeft();   velocity.x = 0; }
	if (location.x > borders.getRight())  { location.x = borders.getRight();  velocity.x = 0; }
	if (location.y < borders.getTop())    { location.y = borders.getTop();    velocity.y = 0; }
	if (location.y > borders.getBottom()) { location.y = borders.getBottom(); velocity.y = 0; }
}

bool SonicParticle::update(HandField & handField, std::vector<Tangible> & tangibles, SonicEngine & engine)
{
	ofVec2f grad = kinectProjector->gradientAtKinectCoord(location.x, location.y);
	acceleration = grad * GRAVITY * Critter::GRADIENT_SIGN;

	// Same motion-aware hand interaction as Critter - see Critter.cpp for
	// the reasoning (guide while moving, hard trap once the hand is still).
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

			location += normal * overlap * (t.getMass() / totalMass);

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

	velocity += acceleration;
	velocity *= DAMPING;
	location += velocity;

	clampToBorders();

	projectorCoord = kinectProjector->kinectCoordToProjCoord(location.x, location.y);

	age += ofGetLastFrameTime();
	bool alive = age < lifetime;

	if (alive && voiceIndex >= 0) {
		float elevation = kinectProjector->elevationAtKinectCoord(location.x, location.y);
		float volume = ofMap(elevation, HEIGHT_MIN, HEIGHT_MAX, 0.0f, 1.0f, true);
		if (INVERT_HEIGHT) volume = 1.0f - volume;

		// Fade in/out at the edges of life so appearing/vanishing doesn't
		// click even before the engine's own release fade kicks in.
		float fadeTime = 0.3f;
		float lifeFade = 1.0f;
		if (age < fadeTime) lifeFade = age / fadeTime;
		else if (lifetime - age < fadeTime) lifeFade = (lifetime - age) / fadeTime;
		lifeFade = ofClamp(lifeFade, 0.0f, 1.0f);

		float speed = velocity.length();
		float freq = ofMap(speed, 0.0f, MAX_SPEED_FOR_PITCH, SonicEngine::MIN_FREQ, SonicEngine::MAX_FREQ, true);

		float panX = ofMap(location.x, borders.getLeft(), borders.getRight(), -1.0f, 1.0f, true);
		float panY = ofMap(location.y, borders.getTop(), borders.getBottom(), -1.0f, 1.0f, true);

		engine.setVoice(voiceIndex, freq, volume * lifeFade, panX, panY);
	}

	if (!alive && voiceIndex >= 0) {
		engine.releaseVoice(voiceIndex);
		voiceIndex = -1;
	}

	return alive;
}

void SonicParticle::release(SonicEngine & engine)
{
	if (voiceIndex >= 0) {
		engine.releaseVoice(voiceIndex);
		voiceIndex = -1;
	}
}

void SonicParticle::draw()
{
	float elevation = kinectProjector->elevationAtKinectCoord(location.x, location.y);
	float volume = ofClamp(ofMap(elevation, HEIGHT_MIN, HEIGHT_MAX, 0.0f, 1.0f), 0.0f, 1.0f);
	if (INVERT_HEIGHT) volume = 1.0f - volume;

	ofPushMatrix();
	ofPushStyle();
	ofTranslate(projectorCoord);

	ofSetColor(120, 200, 255, 160 + (int)(volume * 95));
	ofFill();
	ofDrawCircle(0, 0, radius * (0.8f + volume * 0.6f));
	ofNoFill();

	ofPopStyle();
	ofPopMatrix();
}
