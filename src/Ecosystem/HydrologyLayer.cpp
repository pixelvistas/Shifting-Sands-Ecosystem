#include "HydrologyLayer.h"
#include "ofxImGui.h"
#include <algorithm>

float HydrologyLayer::SPAWN_RATE = 40.0f; // particles/s per active puck - see ECOSIMSPEC.md §6 spawn_rate (default 100, halved: a CPU vector of streak-drawn particles is the thing being proven out here, not the visual density)
int HydrologyLayer::MAX_PARTICLES = 2000;

void HydrologyLayer::setup(std::shared_ptr<KinectProjector> const& k, PuckTracker* tracker)
{
	kinectProjector = k;
	handField.setup(k);
	puckTracker = tracker;
	spawnAccumulator = 0.0f;
	showHandDebug = false;
	manualSpawnCount = 50;
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

	fbo.begin();
	ofClear(255, 255, 255, 0);
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
	ImGui::SliderInt("Particles per spawn", &manualSpawnCount, 1, 500);
	if (ImGui::Button("Add particles") && playArea.width > 0)
		spawnAt(ofPoint(playArea.getCenter()), manualSpawnCount);
	ImGui::SameLine();
	if (ImGui::Button("Clear particles"))
		particles.clear();

	ImGui::End();
}
