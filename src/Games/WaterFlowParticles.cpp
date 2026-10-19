#include "WaterFlowParticles.h"
#include "ofxImGui.h"

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

	for (int tries = 0; tries < 20; tries++) {
		int gx = (int)ofRandom((float)cols);
		int gy = (int)ofRandom((float)rows);
		if (vegetationField->getWaterDepthAt(gx, gy) >= VegetationField::MIN_VISIBLE_WATER_DEPTH_FRAC) {
			p.gx = gx + 0.5f;
			p.gy = gy + 0.5f;
			p.prevGx = p.gx;
			p.prevGy = p.gy;
			return;
		}
	}
	// No wet cell found in 20 tries (e.g. a bone-dry sandbox before any
	// water exists) - park off-grid so draw() skips it cleanly instead of
	// flickering at a meaningless dry position. update() re-tries every
	// later frame via the same depth check that calls this, so a particle
	// finds a home as soon as real water appears, no special-casing needed.
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
			if (vegetationField->getWaterDepthAt(gx, gy) < VegetationField::MIN_VISIBLE_WATER_DEPTH_FRAC) {
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
			ofDrawLine(gridToProjCoord(p.prevGx, p.prevGy), gridToProjCoord(p.gx, p.gy));
		}
	}
	fbo.end();
}

ofPoint WaterFlowParticles::gridToProjCoord(float gx, float gy) const
{
	ofVec2f origin = vegetationField->getGridOrigin();
	float step = vegetationField->getGridStep();
	float kx = origin.x + gx * step;
	float ky = origin.y + gy * step;
	return kinectProjector->kinectCoordToProjCoord(kx, ky);
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
