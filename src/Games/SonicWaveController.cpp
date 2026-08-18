#include "SonicWaveController.h"
#include "ofxImGui.h"

void CSonicWaveController::setup(std::shared_ptr<KinectProjector> const& k)
{
	kinectProjector = k;
	handField.setup(k);
	engine.setup();

	waveActive = false;
	waveProgress = 0.0f;
	waveSpawnAccum = 0.0f;
	waveRestTimer = 0.0f;

	waveDuration = 6.0f;
	waveInterval = 4.0f;
	spawnInterval = 0.15f;
	particlesPerSpawn = 3;
	wavePushSpeed = 0.8f;
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

void CSonicWaveController::spawnWaveParticles()
{
	if (playArea.width <= 0)
		return;

	float waveX = playArea.getLeft() + waveProgress * playArea.getWidth();

	for (int i = 0; i < particlesPerSpawn; i++) {
		float y = ofRandom(playArea.getTop(), playArea.getBottom());
		ofPoint spawnLoc(waveX, y);
		ofVec2f initialVelocity(wavePushSpeed, ofRandom(-0.3f, 0.3f));

		SonicParticle p(kinectProjector, spawnLoc, playArea, initialVelocity);
		p.setup(engine);
		particles.push_back(p);
	}
}

void CSonicWaveController::update()
{
	if (!kinectProjector->isImageStabilized())
		return;

	handField.update();

	float dt = ofGetLastFrameTime();
	if (waveActive) {
		waveProgress += dt / waveDuration;
		waveSpawnAccum += dt;
		if (waveSpawnAccum >= spawnInterval) {
			waveSpawnAccum = 0.0f;
			spawnWaveParticles();
		}
		if (waveProgress >= 1.0f) {
			waveActive = false;
			waveRestTimer = 0.0f;
		}
	} else {
		waveRestTimer += dt;
		if (waveRestTimer >= waveInterval) {
			waveActive = true;
			waveProgress = 0.0f;
			waveSpawnAccum = 0.0f;
		}
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
	ImGui::Text("%d particles | wave %s", (int)particles.size(), waveActive ? "sweeping" : "resting");

	ImGui::Separator();
	ImGui::SliderFloat("Wave duration (s)", &waveDuration, 1.0f, 20.0f);
	ImGui::SliderFloat("Wave rest interval (s)", &waveInterval, 0.0f, 20.0f);
	ImGui::SliderFloat("Spawn interval (s)", &spawnInterval, 0.02f, 1.0f);
	ImGui::SliderInt("Particles per spawn", &particlesPerSpawn, 1, 10);
	ImGui::SliderFloat("Wave push speed", &wavePushSpeed, 0.0f, 3.0f);

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

	if (ImGui::Button("Trigger wave now")) {
		waveActive = true;
		waveProgress = 0.0f;
		waveSpawnAccum = 0.0f;
	}
	ImGui::SameLine();
	if (ImGui::Button("Clear particles")) {
		for (auto & p : particles)
			p.release(engine); // otherwise their voices leak from the pool
		particles.clear();
	}

	ImGui::End();
}
