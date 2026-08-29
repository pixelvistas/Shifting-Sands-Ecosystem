#include "HydrologyParticle.h"
#include "../Games/Critter.h" // GRADIENT_SIGN only - see header note on why it's shared, not duplicated

float HydrologyParticle::GRAVITY_WEIGHT = 0.12f;
float HydrologyParticle::JITTER_WEIGHT = 0.08f;
float HydrologyParticle::JITTER_TURN_RATE = 0.5f;
float HydrologyParticle::OBSTACLE_WEIGHT = 0.3f;
float HydrologyParticle::DRAG = 0.95f;
float HydrologyParticle::MAX_SPEED = 6.0f;

float HydrologyParticle::LIFETIME_MIN = 3.0f;
float HydrologyParticle::LIFETIME_MAX = 7.0f;

float HydrologyParticle::STREAK_LEN = 4.0f;
float HydrologyParticle::STREAK_WIDTH = 1.5f;
ofColor HydrologyParticle::STREAK_COLOR = ofColor(200, 225, 255);

HydrologyParticle::HydrologyParticle(std::shared_ptr<KinectProjector> const& k, ofPoint slocation, ofRectangle sborders)
{
	kinectProjector = k;
	location = slocation;
	borders = sborders;
	velocity = ofVec2f(0);
	age = 0.0f;
	wanderAngle = ofRandom(TWO_PI);
}

void HydrologyParticle::setup()
{
	lifetime = ofRandom(LIFETIME_MIN, LIFETIME_MAX);
}

bool HydrologyParticle::update(HandField & handField)
{
	// Per-frame integration, deliberately NOT scaled by dt - matching
	// Critter::update() exactly (velocity += acceleration; location +=
	// velocity;, no dt anywhere), not ECOSIMSPEC.md §5.2's continuous-time
	// formula. gradientAtKinectCoord()'s raw magnitude was empirically
	// tuned against Critter's per-frame constants (GRAVITY = 0.05, no dt);
	// reintroducing dt here would silently change what every weight below
	// means relative to that tuning, for no benefit at 60fps.

	// Steepest descent - the same field and the same empirically-tuned
	// sign Critter's gravity already uses (see header note).
	ofVec2f g = kinectProjector->gradientAtKinectCoord(location.x, location.y) * Critter::GRADIENT_SIGN;

	// Braiding jitter: a persistent heading nudged by a small random turn
	// each frame, same idiom as Critter::wanderAngle - see header note on
	// why this replaces ECOSIMSPEC.md §5.2's per-frame noise2D sample.
	// This is the parameter that decides whether flow reads as a single
	// deterministic thread or a braided channel network - expect to tune
	// it most, once this is on real hardware.
	wanderAngle += ofRandom(-JITTER_TURN_RATE, JITTER_TURN_RATE);
	ofVec2f j = ofVec2f(cos(wanderAngle), sin(wanderAngle));

	ofVec2f o(0, 0);
	if (handField.isInHand(location.x, location.y)) {
		ofVec2f push = handField.pushDirection(location.x, location.y);
		if (push.lengthSquared() > 0)
			o = push.getNormalized();
	}

	velocity += g * GRAVITY_WEIGHT + j * JITTER_WEIGHT + o * OBSTACLE_WEIGHT;
	velocity *= DRAG; // multiplicative per-frame damping, same role as Critter::DAMPING
	if (velocity.length() > MAX_SPEED)
		velocity = velocity.getNormalized() * MAX_SPEED;

	location += velocity;

	// Age/lifetime stay in real seconds (via dt) - that's a framerate-
	// independent duration, unrelated to the per-frame position update above.
	age += ofGetLastFrameTime();
	bool alive = age < lifetime && borders.inside(location);

	projectorCoord = kinectProjector->kinectCoordToProjCoord(location.x, location.y);

	return alive;
}

void HydrologyParticle::draw()
{
	// Fade in/out at the edges of life, same convention as SonicParticle,
	// so spawning/dying doesn't pop.
	float fadeTime = 0.4f;
	float lifeFade = 1.0f;
	if (age < fadeTime) lifeFade = age / fadeTime;
	else if (lifetime - age < fadeTime) lifeFade = (lifetime - age) / fadeTime;
	lifeFade = ofClamp(lifeFade, 0.0f, 1.0f);

	// Tail offset is computed in kinect-pixel space (where velocity lives)
	// and only then transformed to projector space, same as the head -
	// kinectCoordToProjCoord is a full perspective warp, not a uniform
	// scale, so a kinect-space vector can't be applied after transforming
	// only one endpoint without distorting the streak's on-screen length
	// and direction relative to the real flow.
	ofPoint tailLocation = location - velocity * STREAK_LEN;
	ofVec2f tail = kinectProjector->kinectCoordToProjCoord(tailLocation.x, tailLocation.y);

	ofPushStyle();
	ofEnableBlendMode(OF_BLENDMODE_ADD);
	ofSetColor(STREAK_COLOR, (int)(lifeFade * 180));
	ofSetLineWidth(STREAK_WIDTH);
	ofDrawLine(tail, projectorCoord);
	ofDisableBlendMode();
	ofPopStyle();
}
