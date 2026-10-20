#include "WaterFlowField.h"
#include "ofxImGui.h"
#include <algorithm>
#include <cmath>

bool WaterFlowField::ENABLE_FLOW_FIELD = true;
float WaterFlowField::VISIBLE_WATER_LINE_MM = -1.5f;
int WaterFlowField::MASK_MODE = WaterFlowField::MASK_SIMULATED;
int WaterFlowField::MOTION_MODE = WaterFlowField::MOTION_SIMULATED;
float WaterFlowField::SIM_VELOCITY_SCALE = 1.0f;
int WaterFlowField::PARTICLE_COUNT = 1500;
int WaterFlowField::FIELD_DOWNSAMPLE = 4;
float WaterFlowField::FIELD_BLUR_SIGMA = 1.5f;
float WaterFlowField::FIELD_SMOOTHING_SEC = 0.5f;
float WaterFlowField::GRAVITY = 30.0f;
float WaterFlowField::FRICTION = 2.0f;
float WaterFlowField::MAX_SPEED = 60.0f;
float WaterFlowField::LIFE_MIN_SEC = 2.0f;
float WaterFlowField::LIFE_MAX_SEC = 6.0f;
int WaterFlowField::TRAIL_LENGTH = 20;
float WaterFlowField::TRAIL_THICKNESS = 3.0f;
float WaterFlowField::STILL_BRIGHTNESS = 0.7f;
float WaterFlowField::FULL_BRIGHTNESS_SPEED = 10.0f;
float WaterFlowField::HEAD_SIZE = 4.0f;
float WaterFlowField::PARTICLE_COLOR[3] = { 0.85f, 0.95f, 1.0f };

// Fade-in/out durations (s) - every particle enters and leaves gradually,
// which is what removes the on/off popping the arrow version had.
static const float FADE_IN_SEC = 0.4f;
static const float FADE_OUT_SEC = 0.6f;

void WaterFlowField::setup(std::shared_ptr<KinectProjector> const& k, VegetationField* v)
{
	kinectProjector = k;
	vegetationField = v;
}

void WaterFlowField::setProjectorRes(ofVec2f & PR)
{
	projRes = PR;
}

void WaterFlowField::setKinectROI(ofRectangle & KROI)
{
	kinectROI = KROI;
	fieldReady = false; // grid may have been rebuilt - start the smoothing over
}

ofPoint WaterFlowField::gridToProjCoord(float gx, float gy) const
{
	ofVec2f origin = vegetationField->getGridOrigin();
	float step = vegetationField->getGridStep();
	return kinectProjector->kinectCoordToProjCoord(origin.x + gx * step, origin.y + gy * step);
}

void WaterFlowField::rebuildField(float dt)
{
	const cv::Mat & elev = vegetationField->getElevationGrid();
	int ds = std::max(1, FIELD_DOWNSAMPLE);
	cv::Size fieldSize(std::max(2, elev.cols / ds), std::max(2, elev.rows / ds));

	cv::Mat coarse;
	cv::resize(elev, coarse, fieldSize, 0, 0, cv::INTER_AREA);
	cv::patchNaNs(coarse, 0.0);

	// Exponential smoothing in time: a real sculpting change shows up
	// within ~FIELD_SMOOTHING_SEC, per-frame sensor jitter averages out.
	if (!fieldReady || smoothElevation.size() != coarse.size()) {
		coarse.copyTo(smoothElevation);
		fieldReady = true;
	} else {
		double alpha = 1.0 - std::exp(-dt / std::max(0.01f, FIELD_SMOOTHING_SEC));
		cv::addWeighted(coarse, alpha, smoothElevation, 1.0 - alpha, 0.0, smoothElevation);
	}

	cv::Mat & blurred = fieldElevation;
	if (FIELD_BLUR_SIGMA > 0.0f)
		cv::GaussianBlur(smoothElevation, blurred, cv::Size(0, 0), FIELD_BLUR_SIGMA);
	else
		smoothElevation.copyTo(blurred);

	wetSamples.clear();
	const float * e = blurred.ptr<float>(0); // GaussianBlur/copyTo output is continuous
	for (int i = 0; i < blurred.rows * blurred.cols; i++) {
		bool wet = MASK_MODE == MASK_SIMULATED
			? vegetationField->isWaterCell((i % blurred.cols) * ds + ds / 2, (i / blurred.cols) * ds + ds / 2)
			: e[i] < VISIBLE_WATER_LINE_MM;
		if (wet)
			wetSamples.push_back(i);
	}

	// 3x3 Sobel scaled by 1/8 = central-difference derivative per field
	// sample; divide by ds to get mm per GRID cell, the particles' units.
	double scale = 1.0 / (8.0 * ds);
	cv::Sobel(blurred, gradX, CV_32F, 1, 0, 3, scale);
	cv::Sobel(blurred, gradY, CV_32F, 0, 1, 3, scale);
}

float WaterFlowField::sampleField(const cv::Mat & m, float gx, float gy) const
{
	int ds = std::max(1, FIELD_DOWNSAMPLE);
	int fw = m.cols, fh = m.rows;
	// Field sample i covers grid cells [i*ds, (i+1)*ds) (INTER_AREA), so
	// its center sits at (i + 0.5) * ds.
	float fx = ofClamp(gx / ds - 0.5f, 0.0f, (float)(fw - 1));
	float fy = ofClamp(gy / ds - 0.5f, 0.0f, (float)(fh - 1));
	int x0 = (int)fx, y0 = (int)fy;
	int x1 = std::min(x0 + 1, fw - 1), y1 = std::min(y0 + 1, fh - 1);
	float tx = fx - x0, ty = fy - y0;

	float a = m.at<float>(y0, x0), b = m.at<float>(y0, x1);
	float c = m.at<float>(y1, x0), d = m.at<float>(y1, x1);
	return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty;
}

ofVec2f WaterFlowField::sampleGradient(float gx, float gy) const
{
	return ofVec2f(sampleField(gradX, gx, gy), sampleField(gradY, gx, gy));
}

bool WaterFlowField::isVisibleWater(float gx, float gy) const
{
	if (MASK_MODE == MASK_SIMULATED)
		return vegetationField->isWaterCell((int)gx, (int)gy);
	return sampleField(fieldElevation, gx, gy) < VISIBLE_WATER_LINE_MM;
}

void WaterFlowField::respawn(Particle & p)
{
	p.trailHead = 0;
	p.trailCount = 0;
	if (wetSamples.empty()) {
		p.active = false; // no visible water anywhere - retried every frame
		return;
	}
	int ds = std::max(1, FIELD_DOWNSAMPLE);
	int fw = fieldElevation.cols;
	int idx = wetSamples[(size_t)ofRandom(0.0f, (float)wetSamples.size()) % wetSamples.size()];
	p.pos.set(((idx % fw) + ofRandom(1.0f)) * ds, ((idx / fw) + ofRandom(1.0f)) * ds);
	// Jitter near a shoreline sample can land on the dry side of the
	// bilinear boundary; the sample's own center is wet by construction.
	if (!isVisibleWater(p.pos.x, p.pos.y))
		p.pos.set(((idx % fw) + 0.5f) * ds, ((idx / fw) + 0.5f) * ds);
	p.active = true;
	p.vel.set(0.0f, 0.0f);
	p.age = 0.0f;
	p.life = ofRandom(LIFE_MIN_SEC, std::max(LIFE_MIN_SEC, LIFE_MAX_SEC));
}

void WaterFlowField::update()
{
	trailMesh.clear();
	if (!ENABLE_FLOW_FIELD || !vegetationField || !kinectProjector)
		return;

	int cols = vegetationField->getCols();
	int rows = vegetationField->getRows();
	const cv::Mat & elev = vegetationField->getElevationGrid();
	if (cols <= 0 || rows <= 0 || elev.empty() || elev.cols != cols || elev.rows != rows)
		return;

	float dt = ofClamp((float)ofGetLastFrameTime(), 0.0f, 0.1f);
	rebuildField(dt);

	// Grow/shrink to the requested count; new particles get a random
	// starting age so they don't all fade in and out in lockstep.
	int target = std::max(0, PARTICLE_COUNT);
	if ((int)particles.size() != target) {
		int old = (int)particles.size();
		particles.resize(target);
		for (int i = old; i < target; i++) {
			respawn(particles[i]);
			particles[i].age = ofRandom(0.0f, particles[i].life);
		}
	}

	float decay = std::exp(-FRICTION * dt);
	for (Particle & p : particles) {
		p.age += dt;
		if (!p.active || p.age >= p.life || p.pos.x < 0 || p.pos.y < 0 || p.pos.x >= cols || p.pos.y >= rows)
			respawn(p);
		if (!p.active)
			continue;

		// Downhill = negative elevation gradient (elevation is positive
		// for mounds, negative for digs).
		if (MOTION_MODE == MOTION_SIMULATED) {
			// Carried by the hydrology model's own velocity (cells/s).
			p.vel = vegetationField->getFlowVelocityAt((int)p.pos.x, (int)p.pos.y) * SIM_VELOCITY_SCALE;
		} else {
			ofVec2f g = sampleGradient(p.pos.x, p.pos.y);
			p.vel = p.vel * decay - g * (GRAVITY * dt);
		}
		float speed = p.vel.length();
		if (speed > MAX_SPEED)
			p.vel *= MAX_SPEED / speed;
		ofVec2f next = p.pos + p.vel * dt;

		if (next.x < 0 || next.y < 0 || next.x >= cols || next.y >= rows) {
			respawn(p); // ran off the sandbox edge
			continue;
		}
		if (!isVisibleWater(next.x, next.y)) {
			// Hit the shore: stay put on the water side and fade out, so
			// nothing is ever drawn on land.
			p.vel.set(0.0f, 0.0f);
			p.life = std::min(p.life, p.age + FADE_OUT_SEC);
		} else {
			p.pos = next;
		}

		p.trailHead = (p.trailHead + 1) % MAX_TRAIL;
		ofPoint pp = gridToProjCoord(p.pos.x, p.pos.y);
		p.trail[p.trailHead].set(pp.x, pp.y);
		if (p.trailCount < MAX_TRAIL)
			p.trailCount++;
	}

	// One triangle mesh: each trail segment is a quad TRAIL_THICKNESS px
	// wide and each head a HEAD_SIZE px square. Filled triangles render at
	// the requested size on every driver; GL_LINES wider than 1px are
	// silently drawn 1px wide on core-profile contexts, which is what made
	// the line version nearly invisible on the sand (2026-10-20).
	trailMesh.setMode(OF_PRIMITIVE_TRIANGLES);
	int maxLen = (int)ofClamp((float)TRAIL_LENGTH, 2.0f, (float)MAX_TRAIL);
	float r = PARTICLE_COLOR[0], gC = PARTICLE_COLOR[1], b = PARTICLE_COLOR[2];
	float halfW = 0.5f * TRAIL_THICKNESS;
	float halfHead = 0.5f * HEAD_SIZE;
	auto addQuad = [&](ofVec2f p0, ofVec2f p1, ofVec2f p2, ofVec2f p3, float a01, float a23) {
		// p0,p1 at one end, p2,p3 at the other
		ofFloatColor c01(r, gC, b, a01), c23(r, gC, b, a23);
		trailMesh.addVertex(ofVec3f(p0.x, p0.y, 0)); trailMesh.addColor(c01);
		trailMesh.addVertex(ofVec3f(p1.x, p1.y, 0)); trailMesh.addColor(c01);
		trailMesh.addVertex(ofVec3f(p2.x, p2.y, 0)); trailMesh.addColor(c23);
		trailMesh.addVertex(ofVec3f(p1.x, p1.y, 0)); trailMesh.addColor(c01);
		trailMesh.addVertex(ofVec3f(p3.x, p3.y, 0)); trailMesh.addColor(c23);
		trailMesh.addVertex(ofVec3f(p2.x, p2.y, 0)); trailMesh.addColor(c23);
	};
	for (const Particle & p : particles) {
		int n = std::min(p.trailCount, maxLen);
		if (!p.active || n < 1)
			continue;
		float envelope = std::min(1.0f, p.age / FADE_IN_SEC) * std::min(1.0f, (p.life - p.age) / FADE_OUT_SEC);
		float brightness = ofLerp(STILL_BRIGHTNESS, 1.0f, ofClamp(p.vel.length() / std::max(0.1f, FULL_BRIGHTNESS_SPEED), 0.0f, 1.0f));
		float base = ofClamp(envelope * brightness, 0.0f, 1.0f);

		const ofVec2f & head = p.trail[p.trailHead];
		addQuad(head + ofVec2f(-halfHead, -halfHead), head + ofVec2f(halfHead, -halfHead),
			head + ofVec2f(-halfHead, halfHead), head + ofVec2f(halfHead, halfHead), base, base);

		for (int i = 0; i < n - 1; i++) {
			const ofVec2f & a = p.trail[(p.trailHead - i + MAX_TRAIL) % MAX_TRAIL];
			const ofVec2f & c = p.trail[(p.trailHead - i - 1 + MAX_TRAIL) % MAX_TRAIL];
			ofVec2f d = c - a;
			float len = d.length();
			if (len < 0.01f)
				continue;
			ofVec2f nrm = ofVec2f(-d.y, d.x) * (halfW / len);
			float a0 = base * (1.0f - (float)i / n);
			float a1 = base * (1.0f - (float)(i + 1) / n);
			addQuad(a + nrm, a - nrm, c + nrm, c - nrm, a0, a1);
		}
	}
}

void WaterFlowField::drawMainWindow(float x, float y, float width, float height)
{
	if (projRes.x <= 0 || projRes.y <= 0 || trailMesh.getNumVertices() == 0)
		return;
	ofPushMatrix();
	ofTranslate(x, y);
	ofScale(width / projRes.x, height / projRes.y);
	drawProjectorWindow();
	ofPopMatrix();
}

void WaterFlowField::drawProjectorWindow()
{
	if (trailMesh.getNumVertices() == 0)
		return;
	ofPushStyle();
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
	trailMesh.draw();
	ofPopStyle();
}

void WaterFlowField::drawGui()
{
	ImGui::Begin("Water Flow");
	ImGui::Text("Particles appear on visible water and run downhill along");
	ImGui::Text("the smoothed bed gradient, fading out at the shore.");
	ImGui::Text("Visual only - does not affect the simulation.");
	ImGui::Checkbox("Enable flow particles", &ENABLE_FLOW_FIELD);
	{
		static const char * masks[] = { "Colour map (visible blue)", "Simulated water" };
		static const char * motions[] = { "Bed slope", "Simulated velocity" };
		ImGui::Combo("Particles live on", &MASK_MODE, masks, 2);
		ImGui::Combo("Particles moved by", &MOTION_MODE, motions, 2);
	}
	ImGui::SliderFloat("Simulated velocity scale (visual only)", &SIM_VELOCITY_SCALE, 0.0f, 10.0f);
	ImGui::SliderFloat("Visible water line (mm)", &VISIBLE_WATER_LINE_MM, -45.0f, 20.0f);
	ImGui::Text("Particles only below this elevation; match it to where");
	ImGui::Text("HeightColorMap.xml turns from blue to green (~-1.5mm).");
	ImGui::SliderInt("Particle count", &PARTICLE_COUNT, 0, 6000);

	ImGui::Separator();
	ImGui::Text("Field (noise suppression)");
	ImGui::SliderInt("Downsample (cells/sample)", &FIELD_DOWNSAMPLE, 1, 16);
	ImGui::SliderFloat("Spatial blur (samples)", &FIELD_BLUR_SIGMA, 0.0f, 6.0f);
	ImGui::SliderFloat("Temporal smoothing (s)", &FIELD_SMOOTHING_SEC, 0.0f, 3.0f);

	ImGui::Separator();
	ImGui::Text("Particle motion");
	ImGui::SliderFloat("Gravity", &GRAVITY, 0.0f, 200.0f);
	ImGui::SliderFloat("Friction (1/s)", &FRICTION, 0.0f, 10.0f);
	ImGui::SliderFloat("Max speed (cells/s)", &MAX_SPEED, 1.0f, 300.0f);
	ImGui::SliderFloat("Life min (s)", &LIFE_MIN_SEC, 0.2f, 20.0f);
	ImGui::SliderFloat("Life max (s)", &LIFE_MAX_SEC, 0.2f, 20.0f);

	ImGui::Separator();
	ImGui::Text("Appearance");
	ImGui::SliderInt("Trail length (frames)", &TRAIL_LENGTH, 2, MAX_TRAIL);
	ImGui::SliderFloat("Trail thickness", &TRAIL_THICKNESS, 0.5f, 8.0f);
	ImGui::SliderFloat("Head size (px)", &HEAD_SIZE, 0.0f, 12.0f);
	ImGui::SliderFloat("Still-particle brightness", &STILL_BRIGHTNESS, 0.0f, 1.0f);
	ImGui::SliderFloat("Full-brightness speed (cells/s)", &FULL_BRIGHTNESS_SPEED, 0.5f, 60.0f);
	ImGui::ColorEdit3("Particle color", PARTICLE_COLOR);

	// Measure-before-tuning readout: slope at the ROI center (mm/cell) and
	// the terminal speed it implies, to set GRAVITY/FRICTION from numbers.
	if (fieldReady && vegetationField && vegetationField->getCols() > 0) {
		ofVec2f g = sampleGradient(vegetationField->getCols() * 0.5f, vegetationField->getRows() * 0.5f);
		float terminal = FRICTION > 0.0f ? GRAVITY * g.length() / FRICTION : 0.0f;
		ImGui::Text("ROI-center slope: %.3f mm/cell -> terminal speed ~%.1f cells/s",
			g.length(), terminal);
	}
	int active = 0;
	for (const Particle & p : particles)
		active += p.active ? 1 : 0;
	ImGui::Text("Active particles: %d / %d, wet field samples: %d, trail vertices: %d",
		active, (int)particles.size(), (int)wetSamples.size(), (int)trailMesh.getNumVertices());

	ImGui::End();
}
