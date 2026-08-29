#include "HydrologyLayer.h"
#include "ofxImGui.h"
#include <algorithm>

float HydrologyLayer::SPAWN_RATE = 40.0f; // particles/s per active puck - see ECOSIMSPEC.md §6 spawn_rate (default 100, halved: a CPU vector of streak-drawn particles is the thing being proven out here, not the visual density)
int HydrologyLayer::MAX_PARTICLES = 2000;
float HydrologyLayer::MOISTURE_DEPOSIT_RATE = 0.5f;
float HydrologyLayer::MOISTURE_DECAY_RATE = 0.08f;

namespace {
	const int MOISTURE_GRID_STEP = 12; // kinect px per cell - coarser than particle motion, fine enough for vegetation to read basins vs. slopes
}

void HydrologyLayer::setup(std::shared_ptr<KinectProjector> const& k, PuckTracker* tracker)
{
	kinectProjector = k;
	handField.setup(k);
	puckTracker = tracker;
	spawnAccumulator = 0.0f;
	showHandDebug = false;
	showMoistureDebug = false;
	manualSpawnCount = 50;
	moistureStep = MOISTURE_GRID_STEP;
	moistureCols = 0;
	moistureRows = 0;
}

void HydrologyLayer::setProjectorRes(ofVec2f & PR)
{
	projRes = PR;
	fbo.allocate(projRes.x, projRes.y, GL_RGBA);
}

void HydrologyLayer::setKinectRes(ofVec2f & KR)
{
	kinectRes = KR;
}

void HydrologyLayer::setKinectROI(ofRectangle & KROI)
{
	kinectROI = KROI;
	playArea = kinectROI;
	playArea.scaleFromCenter(0.9f, 0.9f); // 5% margin, same convention as CritterController

	if (kinectROI.width <= 0 || kinectROI.height <= 0)
		return;

	int newCols = std::max(1, (int)(kinectROI.width / moistureStep));
	int newRows = std::max(1, (int)(kinectROI.height / moistureStep));
	if (newCols == moistureCols && newRows == moistureRows)
		return; // dimensions unchanged - keep accumulated moisture, same convention as MyceliumNetwork's pattern grid

	moistureCols = newCols;
	moistureRows = newRows;
	moisture = cv::Mat::zeros(moistureRows, moistureCols, CV_32F);
}

bool HydrologyLayer::moistureGridCoordAt(float kinectX, float kinectY, int & gx, int & gy) const
{
	if (moisture.empty())
		return false;
	gx = (int)((kinectX - kinectROI.x) / moistureStep);
	gy = (int)((kinectY - kinectROI.y) / moistureStep);
	return gx >= 0 && gx < moistureCols && gy >= 0 && gy < moistureRows;
}

float HydrologyLayer::getMoistureAt(float kinectX, float kinectY) const
{
	int gx, gy;
	if (!moistureGridCoordAt(kinectX, kinectY, gx, gy))
		return 0.0f;
	return moisture.at<float>(gy, gx);
}

void HydrologyLayer::updateMoistureGrid(float dt)
{
	if (moisture.empty())
		return;

	moisture *= std::max(0.0f, 1.0f - MOISTURE_DECAY_RATE * dt); // evaporation

	for (auto & p : particles) {
		int gx, gy;
		if (!moistureGridCoordAt(p.getLocation().x, p.getLocation().y, gx, gy))
			continue;
		float & cell = moisture.at<float>(gy, gx);
		cell = std::min(1.0f, cell + MOISTURE_DEPOSIT_RATE * dt);
	}
}

void HydrologyLayer::spawnAt(const ofPoint & loc, int n)
{
	if (playArea.width <= 0)
		return;

	for (int i = 0; i < n && (int)particles.size() < MAX_PARTICLES; i++) {
		// Small random offset so a burst doesn't spawn as one overlapping
		// point - same purpose as SonicParticle's ring spread, just
		// isotropic here since there's no ring shape to preserve.
		ofPoint p = loc + ofPoint(ofRandom(-6.0f, 6.0f), ofRandom(-6.0f, 6.0f));
		HydrologyParticle particle(kinectProjector, p, playArea);
		particle.setup();
		particles.push_back(particle);
	}
}

void HydrologyLayer::update()
{
	if (!kinectProjector->isImageStabilized())
		return;

	handField.update();

	bool puckPresent = puckTracker && puckTracker->isPuckPresent();
	if (puckPresent) {
		spawnAccumulator += SPAWN_RATE * ofGetLastFrameTime();
		int n = (int)spawnAccumulator;
		if (n > 0) {
			spawnAt(puckTracker->getPuckLocation(), n);
			spawnAccumulator -= n;
		}
	}

	particles.erase(
		std::remove_if(particles.begin(), particles.end(),
			[&](HydrologyParticle & p) { return !p.update(handField); }),
		particles.end());

	updateMoistureGrid(ofGetLastFrameTime());

	fbo.begin();
	ofClear(255, 255, 255, 0);

	if (showMoistureDebug && !moisture.empty()) {
		// Rough visual sanity check that deposition/decay is doing
		// something at all, independent of whether particle streaks
		// themselves are visible - a blue-tinted square per cell, alpha
		// by moisture value.
		ofPushStyle();
		ofFill();
		for (int gy = 0; gy < moistureRows; gy++) {
			for (int gx = 0; gx < moistureCols; gx++) {
				float m = moisture.at<float>(gy, gx);
				if (m <= 0.01f) continue;
				float kx = kinectROI.x + gx * moistureStep;
				float ky = kinectROI.y + gy * moistureStep;
				ofVec2f p0 = kinectProjector->kinectCoordToProjCoord(kx, ky);
				ofVec2f p1 = kinectProjector->kinectCoordToProjCoord(kx + moistureStep, ky + moistureStep);
				ofSetColor(60, 120, 255, (int)(m * 150));
				ofDrawRectangle(p0.x, p0.y, p1.x - p0.x, p1.y - p0.y);
			}
		}
		ofPopStyle();
	}

	for (auto & p : particles)
		p.draw();

	if (showHandDebug)
		handField.draw(10, 10, 160, 120);
	fbo.end();
}

void HydrologyLayer::drawMainWindow(float x, float y, float width, float height)
{
	fbo.draw(x, y, width, height);
}

void HydrologyLayer::drawProjectorWindow()
{
	fbo.draw(0, 0);
}

void HydrologyLayer::drawGui()
{
	ImGui::Begin("Hydrology");
	ImGui::Text("%d / %d particles", (int)particles.size(), MAX_PARTICLES);

	bool puckPresent = puckTracker && puckTracker->isPuckPresent();
	if (puckPresent)
		ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.55f, 1.0f), "Puck: DETECTED - spawning at %.0f/s", SPAWN_RATE);
	else
		ImGui::Text("Puck: not detected - use \"Add particles\" below to test flow without it");

	ImGui::SliderFloat("Spawn rate (puck)", &SPAWN_RATE, 0.0f, 300.0f);
	ImGui::SliderInt("Max particles", &MAX_PARTICLES, 100, 10000);

	ImGui::Separator();
	ImGui::Text("Per-frame physics (same convention as Critters, not dt-scaled):");
	ImGui::SliderFloat("Gravity weight", &HydrologyParticle::GRAVITY_WEIGHT, 0.0f, 1.0f);
	ImGui::SliderFloat("Jitter weight", &HydrologyParticle::JITTER_WEIGHT, 0.0f, 0.5f);
	ImGui::SliderFloat("Jitter turn rate", &HydrologyParticle::JITTER_TURN_RATE, 0.0f, 2.0f);
	ImGui::SliderFloat("Obstacle weight", &HydrologyParticle::OBSTACLE_WEIGHT, 0.0f, 2.0f);
	ImGui::SliderFloat("Drag (damping)", &HydrologyParticle::DRAG, 0.5f, 0.99f);
	ImGui::SliderFloat("Max speed (px/frame)", &HydrologyParticle::MAX_SPEED, 0.5f, 30.0f);
	ImGui::SliderFloat("Lifetime min (s)", &HydrologyParticle::LIFETIME_MIN, 0.5f, 20.0f);
	ImGui::SliderFloat("Lifetime max (s)", &HydrologyParticle::LIFETIME_MAX, 0.5f, 20.0f);

	ImGui::Text("Gradient sign and hand tuning are shared with Critters - see that panel.");
	ImGui::Checkbox("Show hand mask", &showHandDebug);

	ImGui::Separator();
	ImGui::Text("Moisture (feeds VegetationLayer's SI_moist):");
	ImGui::SliderFloat("Deposit rate", &MOISTURE_DEPOSIT_RATE, 0.0f, 3.0f);
	ImGui::SliderFloat("Decay rate", &MOISTURE_DECAY_RATE, 0.0f, 1.0f);
	ImGui::Checkbox("Show moisture overlay", &showMoistureDebug);

	ImGui::Separator();
	ImGui::SliderInt("Particles per spawn", &manualSpawnCount, 1, 500);
	if (ImGui::Button("Add particles") && playArea.width > 0)
		spawnAt(ofPoint(playArea.getCenter()), manualSpawnCount);
	ImGui::SameLine();
	if (ImGui::Button("Clear particles"))
		particles.clear();

	ImGui::End();
}
