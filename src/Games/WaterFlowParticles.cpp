#include "WaterFlowParticles.h"
#include "ofxImGui.h"
#include <algorithm>

bool WaterFlowParticles::ENABLE_FLOW_PARTICLES = true;
int WaterFlowParticles::PARTICLE_COUNT = 400;
float WaterFlowParticles::PARTICLE_SPEED_SCALE = 2000.0f;
float WaterFlowParticles::PARTICLE_THICKNESS = 2.0f;
float WaterFlowParticles::PARTICLE_COLOR[3] = { 0.85f, 0.95f, 1.0f };

void WaterFlowParticles::setup(std::shared_ptr<KinectProjector> const& k, VegetationField* v)
{
	kinectProjector = k;
	vegetationField = v;
}

void WaterFlowParticles::setProjectorRes(ofVec2f & PR)
{
	projRes = PR;
	fbo.allocate(projRes.x, projRes.y, GL_RGBA);
}

void WaterFlowParticles::setKinectROI(ofRectangle & KROI)
{
	kinectROI = KROI;
}

void WaterFlowParticles::respawnParticle(Particle & p)
{
	int cols = vegetationField ? vegetationField->getCols() : 0;
	int rows = vegetationField ? vegetationField->getRows() : 0;
	if (cols <= 0 || rows <= 0) {
		p.gx = p.gy = p.prevGx = p.prevGy = -1000.0f;
		return;
	}

	// Bounded linear scan from a random start, wrapping around the whole
	// grid - see the header note on why this replaced an independent
	// random-sample search (confirmed on real hardware to leave most
	// particles permanently parked once the wet fraction was small: each
	// of the old 20 tries was an independent coin flip blind to where the
	// water actually is, so it silently degraded to near-zero success
	// rate rather than failing loudly). The cap (6000 of up to ~153K
	// cells on this project's typical grid) is still cheap - this only
	// runs on an actual respawn, not every particle every frame.
	int total = cols * rows;
	int scanLimit = std::min(total, 6000);
	int start = (int)ofRandom((float)total);
	for (int i = 0; i < scanLimit; i++) {
		int idx = (start + i) % total;
		int gx = idx % cols, gy = idx / cols;
		ofVec2f kc = gridToKinectCoord(gx + 0.5f, gy + 0.5f);
		if (vegetationField->isWaterAt(kc.x, kc.y)) {
			p.gx = gx + 0.5f;
			p.gy = gy + 0.5f;
			p.prevGx = p.gx;
			p.prevGy = p.gy;
			return;
		}
	}
	// Scan covered scanLimit cells and found nothing (e.g. a bone-dry
	// sandbox before any water exists) - park off-grid so draw() skips it
	// cleanly. update() re-tries every later frame via the same
	// isWaterAt() check that calls this, so a particle finds a home as
	// soon as real water appears, no special-casing needed.
	p.gx = p.gy = p.prevGx = p.prevGy = -1000.0f;
}

void WaterFlowParticles::update()
{
	if (!vegetationField || vegetationField->getCols() <= 0)
		return;

	int cols = vegetationField->getCols();
	int rows = vegetationField->getRows();

	if ((int)particles.size() != PARTICLE_COUNT) {
		size_t oldSize = particles.size();
		particles.resize(PARTICLE_COUNT);
		for (size_t i = oldSize; i < particles.size(); i++)
			respawnParticle(particles[i]);
	}

	if (ENABLE_FLOW_PARTICLES) {
		float dt = ofGetLastFrameTime();
		if (dt <= 0.0f || dt > 0.1f)
			dt = 1.0f / 60.0f;

		for (auto & p : particles) {
			p.prevGx = p.gx;
			p.prevGy = p.gy;

			if (p.gx < 0.0f || p.gx >= (float)cols || p.gy < 0.0f || p.gy >= (float)rows) {
				respawnParticle(p);
				continue;
			}

			int gx = (int)p.gx, gy = (int)p.gy;
			ofVec2f kc = gridToKinectCoord(p.gx, p.gy);
			if (!vegetationField->isWaterAt(kc.x, kc.y)) {
				respawnParticle(p);
				continue;
			}

			ofVec2f v = vegetationField->getFlowVelocityAt(gx, gy);
			p.gx += v.x * dt * PARTICLE_SPEED_SCALE;
			p.gy += v.y * dt * PARTICLE_SPEED_SCALE;

			if (p.gx < 0.0f || p.gx >= (float)cols || p.gy < 0.0f || p.gy >= (float)rows)
				respawnParticle(p);
		}
	}

	fbo.begin();
	ofClear(255, 255, 255, 0);
	if (ENABLE_FLOW_PARTICLES) {
		ofSetColor(PARTICLE_COLOR[0] * 255.0f, PARTICLE_COLOR[1] * 255.0f, PARTICLE_COLOR[2] * 255.0f);
		ofSetLineWidth(PARTICLE_THICKNESS);
		for (auto & p : particles) {
			if (p.gx < 0.0f || p.gx >= (float)cols || p.gy < 0.0f || p.gy >= (float)rows)
				continue;
			ofPoint cur = gridToProjCoord(p.gx, p.gy);
			// A near-stationary particle (flat/undisturbed water, no real
			// surface-height difference to drive flow there) would draw as
			// a zero- or near-zero-length line - confirmed on real
			// hardware to render as an inconsistent, flickering "blinking"
			// pixel rather than a stable mark, since degenerate lines are
			// an edge case some GL line-rendering paths don't handle
			// consistently frame to frame. The dot guarantees a stable,
			// always-visible mark regardless of how much (or little) the
			// particle actually moved this frame; the line on top of it
			// still shows real direction/distance whenever there's
			// genuine motion to show.
			ofDrawCircle(cur, std::max(1.0f, PARTICLE_THICKNESS * 0.6f));
			ofDrawLine(gridToProjCoord(p.prevGx, p.prevGy), cur);
		}
	}
	fbo.end();
}

ofPoint WaterFlowParticles::gridToProjCoord(float gx, float gy) const
{
	ofVec2f kc = gridToKinectCoord(gx, gy);
	return kinectProjector->kinectCoordToProjCoord(kc.x, kc.y);
}

ofVec2f WaterFlowParticles::gridToKinectCoord(float gx, float gy) const
{
	ofVec2f origin = vegetationField->getGridOrigin();
	float step = vegetationField->getGridStep();
	return ofVec2f(origin.x + gx * step, origin.y + gy * step);
}

void WaterFlowParticles::drawMainWindow(float x, float y, float width, float height)
{
	fbo.draw(x, y, width, height);
}

void WaterFlowParticles::drawProjectorWindow()
{
	fbo.draw(0, 0);
}

void WaterFlowParticles::drawGui()
{
	ImGui::Begin("Water Flow");
	ImGui::Text("Streaks follow VegetationField's real simulated water");
	ImGui::Text("transport (stepFlow()) - not a second physics system;");
	ImGui::Text("velocity is read directly from its own surface-height data.");
	ImGui::Checkbox("Enable flow particles", &ENABLE_FLOW_PARTICLES);
	ImGui::SliderInt("Particle count", &PARTICLE_COUNT, 0, 2000);
	ImGui::SliderFloat("Speed scale (visualization only)", &PARTICLE_SPEED_SCALE, 0.0f, 10000.0f);
	ImGui::SliderFloat("Streak thickness", &PARTICLE_THICKNESS, 0.5f, 10.0f);
	ImGui::ColorEdit3("Particle color", PARTICLE_COLOR);

	// Same "measure before tuning" posture as every other panel in this
	// project - lets the speed-scale slider above be set from a real
	// number instead of by feel.
	if (vegetationField && vegetationField->getCols() > 0) {
		int cx = vegetationField->getCols() / 2;
		int cy = vegetationField->getRows() / 2;
		float depth = vegetationField->getWaterDepthAt(cx, cy);
		ofVec2f v = vegetationField->getFlowVelocityAt(cx, cy);
		ImGui::Text("ROI-center depth: %.4f, flow velocity: (%.4f, %.4f), |v|=%.4f",
			depth, v.x, v.y, v.length());
	}

	ImGui::End();
}
