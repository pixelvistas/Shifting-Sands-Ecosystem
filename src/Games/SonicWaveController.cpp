#include "SonicWaveController.h"
#include "ofxImGui.h"

void CSonicWaveController::setup(std::shared_ptr<KinectProjector> const& k, PuckTracker* tracker)
{
	kinectProjector = k;
	handField.setup(k);
	puckTracker = tracker;
	engine.setup();

	waveSpawnAccum = 0.0f;
	showPuckDebug = false;

	spawnInterval = 0.6f;
	ringPointCount = 32;
	wavePushSpeed = 0.6f;
	ringColor = ofColor(90, 230, 255); // neon cyan default
	ringColorRGB[0] = ringColor.r / 255.0f;
	ringColorRGB[1] = ringColor.g / 255.0f;
	ringColorRGB[2] = ringColor.b / 255.0f;
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

void CSonicWaveController::spawnRingAt(const ofPoint & origin)
{
	if (playArea.width <= 0 || ringPointCount < 3)
		return;

	SonicRing ring;
	float lifetime = ofRandom(SonicParticle::LIFETIME_MIN, SonicParticle::LIFETIME_MAX);

	for (int i = 0; i < ringPointCount; i++) {
		float angle = (TWO_PI * i) / ringPointCount;
		ofVec2f dir(cos(angle), sin(angle));
		ofVec2f initialVelocity = dir * wavePushSpeed;

		SonicParticle p(kinectProjector, origin, playArea, initialVelocity);
		p.setup(engine, lifetime);
		ring.points.push_back(p);
	}
	rings.push_back(ring);
}

void CSonicWaveController::update()
{
	if (!kinectProjector->isImageStabilized())
		return;

	handField.update();

	float dt = ofGetLastFrameTime();
	if (puckTracker->isPuckPresent()) {
		waveSpawnAccum += dt;
		if (waveSpawnAccum >= spawnInterval) {
			waveSpawnAccum = 0.0f;
			spawnRingAt(puckTracker->getPuckLocation());
		}
	} else {
		// Reset so the first ring after the puck is placed again isn't
		// delayed by whatever was left over from before it was removed.
		waveSpawnAccum = 0.0f;
	}

	for (size_t r = 0; r < rings.size(); ) {
		bool anyAlive = false;
		auto & points = rings[r].points;
		for (size_t i = 0; i < points.size(); ) {
			bool alive = points[i].update(handField, noTangibles, engine);
			if (alive) {
				anyAlive = true;
				i++;
			} else {
				points.erase(points.begin() + i);
			}
		}
		if (anyAlive) {
			r++;
		} else {
			rings.erase(rings.begin() + r);
		}
	}

	fbo.begin();
	ofClear(255, 255, 255, 0);
	for (auto & ring : rings)
		drawRing(ring);
	if (showPuckDebug) {
		// Fixed-position thumbnail, not spatially registered to the sand -
		// same convention as HandField's debug overlay.
		puckTracker->draw(10, 10, 160, 120);
	}
	fbo.end();
}

void CSonicWaveController::drawRing(const SonicRing & ring)
{
	if (ring.points.size() < 3)
		return;

	ofPolyline line;
	for (auto & p : ring.points) {
		ofVec2f projectorCoord = kinectProjector->kinectCoordToProjCoord(p.getLocation().x, p.getLocation().y);
		line.addVertex(projectorCoord.x, projectorCoord.y);
	}
	line.close();

	ofPushStyle();
	ofEnableBlendMode(OF_BLENDMODE_ADD);

	// Soft outer glow: a few progressively wider, dimmer passes underneath
	// a thin bright core - the standard cheap way to fake a neon bloom
	// without a real post-process blur.
	for (int pass = 4; pass >= 1; pass--) {
		ofSetColor(ringColor, 35);
		ofSetLineWidth(pass * 5.0f);
		line.draw();
	}

	ofSetColor(255, 255, 255, 220);
	ofSetLineWidth(1.5f);
	line.draw();

	ofDisableBlendMode();
	ofPopStyle();
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
	int totalPoints = 0;
	for (auto & ring : rings)
		totalPoints += (int)ring.points.size();

	ImGui::Begin("Sonic Wave");
	ImGui::Text("%d rings (%d points) | puck %s", (int)rings.size(), totalPoints,
		puckTracker->isPuckPresent() ? "DETECTED" : "not detected");

	ImGui::Separator();
	ImGui::Text("Puck detection (shared with the Critters panel)");
	ImGui::SliderFloat("Puck diameter (mm)", &PuckTracker::PUCK_DIAMETER_MM, 10.0f, 300.0f);
	ImGui::SliderFloat("Puck height (mm)", &PuckTracker::PUCK_HEIGHT_MM, 10.0f, 300.0f);
	if (ImGui::Button("Estimate radius from puck size"))
		PuckTracker::EXPECTED_RADIUS_CELLS = puckTracker->estimateRadiusCells();
	ImGui::SameLine();
	if (ImGui::Button("Estimate height threshold"))
		PuckTracker::HEIGHT_THRESHOLD = PuckTracker::PUCK_HEIGHT_MM * 0.4f;
	ImGui::SliderFloat("Expected radius (cells)", &PuckTracker::EXPECTED_RADIUS_CELLS, 2.0f, 30.0f);
	ImGui::SliderFloat("Height threshold (mm)", &PuckTracker::HEIGHT_THRESHOLD, 1.0f, 100.0f);
	ImGui::SliderFloat("Min circularity", &PuckTracker::MIN_CIRCULARITY, 0.1f, 1.0f);
	ImGui::SliderFloat("Confirm time (s)", &PuckTracker::CONFIRM_TIME, 0.0f, 3.0f);
	ImGui::SliderFloat("Max track jump (px)", &PuckTracker::MAX_TRACK_JUMP, 5.0f, 150.0f);
	ImGui::SliderInt("Max lost frames", &PuckTracker::MAX_LOST_FRAMES, 0, 30);
	ImGui::Checkbox("Invert elevation", &PuckTracker::INVERT_ELEVATION);
	ImGui::Checkbox("Show puck detection overlay", &showPuckDebug);

	ImGui::Separator();
	ImGui::Text("Ring (fires while puck is present)");
	ImGui::SliderFloat("Spawn interval (s)", &spawnInterval, 0.05f, 3.0f);
	ImGui::SliderInt("Points per ring", &ringPointCount, 8, 96);
	ImGui::SliderFloat("Ring expansion speed", &wavePushSpeed, 0.0f, 3.0f);
	if (ImGui::ColorEdit3("Ring color", ringColorRGB)) {
		ringColor = ofColor(ringColorRGB[0] * 255.0f, ringColorRGB[1] * 255.0f, ringColorRGB[2] * 255.0f);
	}

	ImGui::Separator();
	ImGui::SliderFloat("Damping", &SonicParticle::DAMPING, 0.5f, 0.99f);
	ImGui::SliderFloat("Hand push (still hand)", &SonicParticle::HAND_PUSH_STRENGTH, 0.0f, 10.0f);
	ImGui::SliderFloat("Herd strength (moving hand)", &SonicParticle::HERD_STRENGTH, 0.0f, 1.0f);

	ImGui::Separator();
	ImGui::SliderFloat("Lifetime min (s)", &SonicParticle::LIFETIME_MIN, 1.0f, 30.0f);
	ImGui::SliderFloat("Lifetime max (s)", &SonicParticle::LIFETIME_MAX, 1.0f, 30.0f);

	if (ImGui::Button("Test ring (center, no puck needed)")) {
		spawnRingAt(ofPoint(playArea.getCenter()));
	}
	ImGui::SameLine();
	if (ImGui::Button("Clear rings")) {
		for (auto & ring : rings)
			for (auto & p : ring.points)
				p.release(engine); // otherwise their voices leak from the pool
		rings.clear();
	}

	ImGui::End();
}
