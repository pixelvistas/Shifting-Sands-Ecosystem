#include "SonicWaveController.h"
#include "ofxImGui.h"

namespace {
	// A ring is meant to persist until its puck moves or disappears, not
	// expire on a timer - HELD_RING_LIFETIME just needs to be "longer than
	// anyone will leave a puck sitting still," not literally infinite.
	const float HELD_RING_LIFETIME = 3600.0f;
	// How long a retired ring takes to fade out and release its voices.
	const float RETIRE_FADE_SECONDS = 0.5f;
}

void CSonicWaveController::setup(std::shared_ptr<KinectProjector> const& k, PuckTracker* tracker)
{
	kinectProjector = k;
	puckTracker = tracker;
	engine.setup();

	hasActiveRing = false;
	showPuckDebug = false;

	ringMoveThreshold = 25.0f;
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
	for (int i = 0; i < ringPointCount; i++) {
		float angle = (TWO_PI * i) / ringPointCount;
		ofVec2f dir(cos(angle), sin(angle));
		ofVec2f initialVelocity = dir * wavePushSpeed;

		SonicParticle p(kinectProjector, origin, playArea, initialVelocity);
		p.setup(engine, HELD_RING_LIFETIME);
		ring.points.push_back(p);
	}
	rings.push_back(ring);
}

void CSonicWaveController::retireRing(SonicRing & ring)
{
	for (auto & p : ring.points)
		p.retire(RETIRE_FADE_SECONDS);
}

void CSonicWaveController::update()
{
	if (!kinectProjector->isImageStabilized())
		return;

	if (puckTracker->isPuckPresent()) {
		ofPoint puckLocation = puckTracker->getPuckLocation();
		bool canSpawn = playArea.width > 0 && ringPointCount >= 3;
		bool puckMoved = hasActiveRing && (puckLocation - activeRingOrigin).length() > ringMoveThreshold;

		if (canSpawn && (!hasActiveRing || puckMoved)) {
			if (hasActiveRing && !rings.empty())
				retireRing(rings.back()); // the ring being replaced - already the most recent, still non-retiring one
			spawnRingAt(puckLocation);
			activeRingOrigin = puckLocation;
			hasActiveRing = true;
		}
	} else if (hasActiveRing) {
		if (!rings.empty())
			retireRing(rings.back());
		hasActiveRing = false;
	}

	for (size_t r = 0; r < rings.size(); ) {
		bool anyAlive = false;
		auto & points = rings[r].points;
		for (size_t i = 0; i < points.size(); ) {
			bool alive = points[i].update(noTangibles, engine);
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
		// Fixed-position thumbnail, not spatially registered to the sand.
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

	// Live rejection diagnostics - shows exactly which filter is stopping
	// detection instead of just a yes/no, since "raised pixels exist but no
	// contour survived" and "nothing raised at all" need completely
	// different fixes (shape/size tuning vs. threshold/invert-elevation).
	if (!puckTracker->getLastAnyRaisedPixels()) {
		ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f),
			"No raised pixels this frame - lower Height threshold or try Invert elevation.");
	} else if (!puckTracker->getLastHasCandidateContour()) {
		ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "Raised pixels found but no contour - unexpected, report this.");
	} else {
		float expectedArea = puckTracker->getExpectedAreaCells();
		double area = puckTracker->getLastCandidateArea();
		double circ = puckTracker->getLastCandidateCircularity();
		bool areaOk = area >= expectedArea * 0.3 && area <= expectedArea * 3.0;
		bool circOk = circ >= PuckTracker::MIN_CIRCULARITY;
		ImGui::TextColored(areaOk ? ImVec4(0.6f, 1.0f, 0.6f, 1.0f) : ImVec4(1.0f, 0.5f, 0.3f, 1.0f),
			"Largest blob area: %.0f cells (need %.0f-%.0f, expected ~%.0f)",
			area, expectedArea * 0.3, expectedArea * 3.0, expectedArea);
		ImGui::TextColored(circOk ? ImVec4(0.6f, 1.0f, 0.6f, 1.0f) : ImVec4(1.0f, 0.5f, 0.3f, 1.0f),
			"Largest blob circularity: %.2f (need >= %.2f)", circ, PuckTracker::MIN_CIRCULARITY);
	}

	ImGui::Separator();
	ImGui::Text("Ring (expands once, holds until the puck moves or is removed)");
	ImGui::SliderFloat("Puck move distance for new ring (px)", &ringMoveThreshold, 5.0f, 150.0f);
	ImGui::SliderInt("Points per ring", &ringPointCount, 8, 96);
	ImGui::SliderFloat("Ring expansion speed", &wavePushSpeed, 0.0f, 3.0f);
	if (ImGui::ColorEdit3("Ring color", ringColorRGB)) {
		ringColor = ofColor(ringColorRGB[0] * 255.0f, ringColorRGB[1] * 255.0f, ringColorRGB[2] * 255.0f);
	}

	ImGui::Separator();
	ImGui::SliderFloat("Damping", &SonicParticle::DAMPING, 0.5f, 0.99f);

	if (ImGui::Button("Test ring (center, no puck needed)")) {
		spawnRingAt(ofPoint(playArea.getCenter()));
	}
	ImGui::SameLine();
	if (ImGui::Button("Clear rings")) {
		for (auto & ring : rings)
			for (auto & p : ring.points)
				p.release(engine); // otherwise their voices leak from the pool
		rings.clear();
		hasActiveRing = false; // so a still-present puck gets a fresh ring next frame instead of waiting for it to move
	}

	ImGui::End();
}
