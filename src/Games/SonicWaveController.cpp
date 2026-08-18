#include "SonicWaveController.h"
#include "ofxImGui.h"

void CSonicWaveController::setup(std::shared_ptr<KinectProjector> const& k)
{
	kinectProjector = k;
	handField.setup(k);
	puckTracker.setup(k);
	engine.setup();

	waveSpawnAccum = 0.0f;
	showPuckDebug = false;

	spawnInterval = 0.6f;
	particlesPerSpawn = 4;
	wavePushSpeed = 0.6f;
}

void CSonicWaveController::exit()
{
	engine.exit();
}

void CSonicWaveController::setProjectorRes(ofVec2f & PR)
{
	projRes = PR;
	fbo.allocate(projRes.x, projRes.y, GL_RGBA);
}

void CSonicWaveController::setKinectRes(ofVec2f & KR)
{
	kinectRes = KR;
}

void CSonicWaveController::setKinectROI(ofRectangle & KROI)
{
	kinectROI = KROI;
	playArea = kinectROI;
	playArea.scaleFromCenter(0.9f, 0.9f); // same margin convention as CCritterController
}

void CSonicWaveController::spawnBurstAt(const ofPoint & origin)
{
	if (playArea.width <= 0)
		return;

	for (int i = 0; i < particlesPerSpawn; i++) {
		float angle = ofRandom(TWO_PI);
		ofVec2f initialVelocity(cos(angle), sin(angle));
		initialVelocity *= wavePushSpeed;

		SonicParticle p(kinectProjector, origin, playArea, initialVelocity);
		p.setup(engine);
		particles.push_back(p);
	}
}

void CSonicWaveController::update()
{
	if (!kinectProjector->isImageStabilized())
		return;

	handField.update();
	puckTracker.update();

	float dt = ofGetLastFrameTime();
	if (puckTracker.isPuckPresent()) {
		waveSpawnAccum += dt;
		if (waveSpawnAccum >= spawnInterval) {
			waveSpawnAccum = 0.0f;
			spawnBurstAt(puckTracker.getPuckLocation());
		}
	} else {
		// Reset so the first burst after the puck is placed again isn't
		// delayed by whatever was left over from before it was removed.
		waveSpawnAccum = 0.0f;
	}

	for (size_t i = 0; i < particles.size(); ) {
		bool alive = particles[i].update(handField, noTangibles, engine);
		if (!alive) {
			particles.erase(particles.begin() + i);
		} else {
			i++;
		}
	}

	fbo.begin();
	ofClear(255, 255, 255, 0);
	for (auto & p : particles)
		p.draw();
	if (showPuckDebug) {
		// Fixed-position thumbnail, not spatially registered to the sand -
		// same convention as HandField's debug overlay.
		puckTracker.draw(10, 10, 160, 120);
	}
	fbo.end();
}

void CSonicWaveController::drawMainWindow(float x, float y, float width, float height)
{
	fbo.draw(x, y, width, height);
}

void CSonicWaveController::drawProjectorWindow()
{
	fbo.draw(0, 0);
}

void CSonicWaveController::drawGui()
{
	ImGui::Begin("Sonic Wave");
	ImGui::Text("%d particles | puck %s", (int)particles.size(),
		puckTracker.isPuckPresent() ? "DETECTED" : "not detected");

	ImGui::Separator();
	ImGui::Text("Puck detection");
	ImGui::SliderFloat("Expected radius (cells)", &PuckTracker::EXPECTED_RADIUS_CELLS, 2.0f, 30.0f);
	ImGui::SliderFloat("Height threshold", &PuckTracker::HEIGHT_THRESHOLD, 1.0f, 100.0f);
	ImGui::SliderFloat("Min circularity", &PuckTracker::MIN_CIRCULARITY, 0.1f, 1.0f);
	ImGui::SliderFloat("Confirm time (s)", &PuckTracker::CONFIRM_TIME, 0.0f, 3.0f);
	ImGui::SliderFloat("Max track jump (px)", &PuckTracker::MAX_TRACK_JUMP, 5.0f, 150.0f);
	ImGui::SliderInt("Max lost frames", &PuckTracker::MAX_LOST_FRAMES, 0, 30);
	ImGui::Checkbox("Invert elevation", &PuckTracker::INVERT_ELEVATION);
	ImGui::Checkbox("Show puck detection overlay", &showPuckDebug);

	ImGui::Separator();
	ImGui::Text("Wave (fires while puck is present)");
	ImGui::SliderFloat("Spawn interval (s)", &spawnInterval, 0.05f, 3.0f);
	ImGui::SliderInt("Particles per burst", &particlesPerSpawn, 1, 15);
	ImGui::SliderFloat("Burst push speed", &wavePushSpeed, 0.0f, 3.0f);

	ImGui::Separator();
	ImGui::SliderFloat("Gravity", &SonicParticle::GRAVITY, 0.0f, 0.3f);
	ImGui::SliderFloat("Damping", &SonicParticle::DAMPING, 0.5f, 0.99f);
	ImGui::SliderFloat("Hand push (still hand)", &SonicParticle::HAND_PUSH_STRENGTH, 0.0f, 10.0f);
	ImGui::SliderFloat("Herd strength (moving hand)", &SonicParticle::HERD_STRENGTH, 0.0f, 1.0f);
	ImGui::Text("Gradient sign is shared with the Critters panel.");

	ImGui::Separator();
	ImGui::SliderFloat("Lifetime min (s)", &SonicParticle::LIFETIME_MIN, 1.0f, 30.0f);
	ImGui::SliderFloat("Lifetime max (s)", &SonicParticle::LIFETIME_MAX, 1.0f, 30.0f);

	ImGui::Separator();
	ImGui::Text("Sonification");
	ImGui::SliderFloat("Height -> volume: min", &SonicParticle::HEIGHT_MIN, -200.0f, 200.0f);
	ImGui::SliderFloat("Height -> volume: max", &SonicParticle::HEIGHT_MAX, -200.0f, 200.0f);
	ImGui::Checkbox("Invert height mapping", &SonicParticle::INVERT_HEIGHT);
	ImGui::SliderFloat("Speed -> pitch: max speed", &SonicParticle::MAX_SPEED_FOR_PITCH, 0.1f, 10.0f);
	ImGui::SliderFloat("Pitch: min Hz", &SonicEngine::MIN_FREQ, 40.0f, 2000.0f);
	ImGui::SliderFloat("Pitch: max Hz", &SonicEngine::MAX_FREQ, 40.0f, 4000.0f);
	ImGui::SliderFloat("Master volume", &SonicEngine::MASTER_GAIN, 0.0f, 1.0f);

	if (ImGui::Button("Test burst (center, no puck needed)")) {
		spawnBurstAt(ofPoint(playArea.getCenter()));
	}
	ImGui::SameLine();
	if (ImGui::Button("Clear particles")) {
		for (auto & p : particles)
			p.release(engine); // otherwise their voices leak from the pool
		particles.clear();
	}

	ImGui::End();
}
