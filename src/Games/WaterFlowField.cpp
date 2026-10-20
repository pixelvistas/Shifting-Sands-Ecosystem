#include "WaterFlowField.h"
#include "ofxImGui.h"
#include <algorithm>
#include <cmath>

bool WaterFlowField::ENABLE_FLOW_FIELD = true;
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
float WaterFlowField::TRAIL_THICKNESS = 2.0f;
float WaterFlowField::STILL_BRIGHTNESS = 0.25f;
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

	cv::Mat blurred;
	if (FIELD_BLUR_SIGMA > 0.0f)
		cv::GaussianBlur(smoothElevation, blurred, cv::Size(0, 0), FIELD_BLUR_SIGMA);
	else
		blurred = smoothElevation;

	// 3x3 Sobel scaled by 1/8 = central-difference derivative per field
	// sample; divide by ds to get mm per GRID cell, the particles' units.
	double scale = 1.0 / (8.0 * ds);
	cv::Sobel(blurred, gradX, CV_32F, 1, 0, 3, scale);
	cv::Sobel(blurred, gradY, CV_32F, 0, 1, 3, scale);
}

ofVec2f WaterFlowField::sampleGradient(float gx, float gy) const
{
	int ds = std::max(1, FIELD_DOWNSAMPLE);
	int fw = gradX.cols, fh = gradX.rows;
	// Field sample i covers grid cells [i*ds, (i+1)*ds) (INTER_AREA), so
	// its center sits at (i + 0.5) * ds.
	float fx = ofClamp(gx / ds - 0.5f, 0.0f, (float)(fw - 1));
	float fy = ofClamp(gy / ds - 0.5f, 0.0f, (float)(fh - 1));
	int x0 = (int)fx, y0 = (int)fy;
	int x1 = std::min(x0 + 1, fw - 1), y1 = std::min(y0 + 1, fh - 1);
	float tx = fx - x0, ty = fy - y0;

	auto bilerp = [&](const cv::Mat & m) {
		float a = m.at<float>(y0, x0), b = m.at<float>(y0, x1);
		float c = m.at<float>(y1, x0), d = m.at<float>(y1, x1);
		return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty;
	};
	return ofVec2f(bilerp(gradX), bilerp(gradY));
}

void WaterFlowField::respawn(Particle & p)
{
	p.pos.set(ofRandom(0.0f, (float)vegetationField->getCols()),
		ofRandom(0.0f, (float)vegetationField->getRows()));
	p.vel.set(0.0f, 0.0f);
	p.age = 0.0f;
	p.life = ofRandom(LIFE_MIN_SEC, std::max(LIFE_MIN_SEC, LIFE_MAX_SEC));
	p.trailHead = 0;
	p.trailCount = 0;
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
		if (p.age >= p.life || p.pos.x < 0 || p.pos.y < 0 || p.pos.x >= cols || p.pos.y >= rows)
			respawn(p);

		// Downhill = negative elevation gradient (elevation is positive
		// for mounds, negative for digs).
		ofVec2f g = sampleGradient(p.pos.x, p.pos.y);
		p.vel = p.vel * decay - g * (GRAVITY * dt);
		float speed = p.vel.length();
		if (speed > MAX_SPEED)
			p.vel *= MAX_SPEED / speed;
		p.pos += p.vel * dt;

		if (p.pos.x < 0 || p.pos.y < 0 || p.pos.x >= cols || p.pos.y >= rows) {
			respawn(p); // ran off the sandbox edge
			continue;
		}

		p.trailHead = (p.trailHead + 1) % MAX_TRAIL;
		ofPoint pp = gridToProjCoord(p.pos.x, p.pos.y);
		p.trail[p.trailHead].set(pp.x, pp.y);
		if (p.trailCount < MAX_TRAIL)
			p.trailCount++;
	}

	// One mesh of line segments, alpha fading from head to tail and
	// scaled by the particle's life envelope and speed.
	trailMesh.setMode(OF_PRIMITIVE_LINES);
	int maxLen = (int)ofClamp((float)TRAIL_LENGTH, 2.0f, (float)MAX_TRAIL);
	float r = PARTICLE_COLOR[0], gC = PARTICLE_COLOR[1], b = PARTICLE_COLOR[2];
	for (const Particle & p : particles) {
		int n = std::min(p.trailCount, maxLen);
		if (n < 2)
			continue;
		float envelope = std::min(1.0f, p.age / FADE_IN_SEC) * std::min(1.0f, (p.life - p.age) / FADE_OUT_SEC);
		float brightness = ofLerp(STILL_BRIGHTNESS, 1.0f, ofClamp(p.vel.length() / (0.5f * MAX_SPEED), 0.0f, 1.0f));
		float base = ofClamp(envelope * brightness, 0.0f, 1.0f);
		for (int i = 0; i < n - 1; i++) {
			const ofVec2f & a = p.trail[(p.trailHead - i + MAX_TRAIL) % MAX_TRAIL];
			const ofVec2f & c = p.trail[(p.trailHead - i - 1 + MAX_TRAIL) % MAX_TRAIL];
			float a0 = base * (1.0f - (float)i / n);
			float a1 = base * (1.0f - (float)(i + 1) / n);
			trailMesh.addVertex(ofVec3f(a.x, a.y, 0));
			trailMesh.addColor(ofFloatColor(r, gC, b, a0));
			trailMesh.addVertex(ofVec3f(c.x, c.y, 0));
			trailMesh.addColor(ofFloatColor(r, gC, b, a1));
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
	ofSetLineWidth(TRAIL_THICKNESS);
	trailMesh.draw();
	ofPopStyle();
}

void WaterFlowField::drawGui()
{
	ImGui::Begin("Water Flow");
	ImGui::Text("Particles 'rain' over the sand and run downhill along");
	ImGui::Text("the smoothed terrain gradient, collecting in basins.");
	ImGui::Text("Visual only - does not affect the simulation.");
	ImGui::Checkbox("Enable flow particles", &ENABLE_FLOW_FIELD);
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
	ImGui::SliderFloat("Still-particle brightness", &STILL_BRIGHTNESS, 0.0f, 1.0f);
	ImGui::ColorEdit3("Particle color", PARTICLE_COLOR);

	// Measure-before-tuning readout: slope at the ROI center (mm/cell) and
	// the terminal speed it implies, to set GRAVITY/FRICTION from numbers.
	if (fieldReady && vegetationField && vegetationField->getCols() > 0) {
		ofVec2f g = sampleGradient(vegetationField->getCols() * 0.5f, vegetationField->getRows() * 0.5f);
		float terminal = FRICTION > 0.0f ? GRAVITY * g.length() / FRICTION : 0.0f;
		ImGui::Text("ROI-center slope: %.3f mm/cell -> terminal speed ~%.1f cells/s",
			g.length(), terminal);
	}
	ImGui::Text("Live particles: %d, trail vertices: %d", (int)particles.size(), (int)trailMesh.getNumVertices());

	ImGui::End();
}
