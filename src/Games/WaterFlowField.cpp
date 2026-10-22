#include "WaterFlowField.h"
#include "ofxImGui.h"
#include <algorithm>
#include <cmath>

bool WaterFlowField::ENABLE_FLOW_FIELD = true;
float WaterFlowField::VISIBLE_WATER_LINE_MM = -1.5f;
int WaterFlowField::MASK_MODE = WaterFlowField::MASK_SIMULATED;
int WaterFlowField::MOTION_MODE = WaterFlowField::MOTION_DEWITZ;
float WaterFlowField::DEWITZ_FADE = 0.08f;
float WaterFlowField::DEWITZ_POINT_ALPHA = 0.6f;
float WaterFlowField::DEWITZ_COLOR[3] = { 0.75f, 0.92f, 1.0f };
int WaterFlowField::DEWITZ_SHOWN_ON = WaterFlowField::DEWITZ_SHOW_WATER_ONLY;
bool WaterFlowField::DEWITZ_SHOW_OBSTACLES = false;
float WaterFlowField::DEWITZ_OBSTACLE_COLOR[4] = { 1.0f, 0.0f, 1.0f, 0.35f };
bool WaterFlowField::EMIT_AT_SOURCE = true;
int WaterFlowField::SOURCE_BAND_CELLS = 6;
float WaterFlowField::STALL_SPEED = 1.0f;
float WaterFlowField::STALL_SEC = 1.5f;
float WaterFlowField::SIM_VELOCITY_SCALE = 1.0f;
int WaterFlowField::PARTICLE_COUNT = 1500;
int WaterFlowField::FIELD_DOWNSAMPLE = 4;
float WaterFlowField::FIELD_BLUR_SIGMA = 1.5f;
float WaterFlowField::FIELD_SMOOTHING_SEC = 0.5f;
float WaterFlowField::GRAVITY = 30.0f;
float WaterFlowField::FRICTION = 2.0f;
float WaterFlowField::MAX_SPEED = 60.0f;
// Long enough to cross the table at stream speed; stalled particles are
// retired early by STALL_SEC instead.
float WaterFlowField::LIFE_MIN_SEC = 15.0f;
float WaterFlowField::LIFE_MAX_SEC = 40.0f;
int WaterFlowField::TRAIL_LENGTH = 60;
float WaterFlowField::TRAIL_THICKNESS = 2.0f;
float WaterFlowField::STILL_BRIGHTNESS = 0.7f;
float WaterFlowField::FULL_BRIGHTNESS_SPEED = 10.0f;
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
	// GL_RGB, no alpha channel: with RGBA, alpha-blending the black fade
	// quad would also erode the FBO's alpha, and the additive composite
	// (which scales by source alpha) would dim over time.
	dewitzFbo.allocate(std::max(1, (int)projRes.x), std::max(1, (int)projRes.y), GL_RGB);
	dewitzFbo.begin();
	ofClear(0, 0, 0, 255);
	dewitzFbo.end();
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
	p.stillTime = 0.0f;
	p.vel.set(0.0f, 0.0f);
	p.age = 0.0f;
	p.life = ofRandom(LIFE_MIN_SEC, std::max(LIFE_MIN_SEC, LIFE_MAX_SEC));
	if (EMIT_AT_SOURCE && !sourceCells.empty()) {
		int idx = sourceCells[(size_t)ofRandom(0.0f, (float)sourceCells.size()) % sourceCells.size()];
		int cols = vegetationField->getCols();
		p.pos.set((idx % cols) + ofRandom(1.0f), (idx / cols) + ofRandom(1.0f));
		p.active = true;
		return;
	}
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
	if (MOTION_MODE == MOTION_DEWITZ) {
		updateDewitz(dt);
		return;
	}
	rebuildField(dt);
	if (MOTION_MODE == MOTION_LATTICE)
		lattice.update(*vegetationField, dt);

	// Wet cells along the source edge - see EMIT_AT_SOURCE.
	sourceCells.clear();
	if (EMIT_AT_SOURCE) {
		int band = std::max(1, SOURCE_BAND_CELLS);
		for (int gy = 0; gy < rows; gy++) {
			for (int gx = 0; gx < cols; gx++) {
				bool inBand;
				switch (VegetationField::SOURCE_EDGE) {
				case VegetationField::EDGE_LEFT:  inBand = gx < band; break;
				case VegetationField::EDGE_RIGHT: inBand = gx >= cols - band; break;
				case VegetationField::EDGE_TOP:   inBand = gy < band; break;
				default:                          inBand = gy >= rows - band; break;
				}
				if (inBand && vegetationField->isWaterCell(gx, gy))
					sourceCells.push_back(gy * cols + gx);
			}
		}
	}

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
		if (MOTION_MODE == MOTION_LATTICE) {
			// Carried by the lattice-Boltzmann velocity (cells/s).
			p.vel = lattice.velocityAt(p.pos.x, p.pos.y) * SIM_VELOCITY_SCALE;
		} else if (MOTION_MODE == MOTION_SIMULATED) {
			// Carried by the hydrology model's own velocity (cells/s).
			p.vel = vegetationField->getFlowVelocityAt((int)p.pos.x, (int)p.pos.y) * SIM_VELOCITY_SCALE;
		} else {
			ofVec2f g = sampleGradient(p.pos.x, p.pos.y);
			p.vel = p.vel * decay - g * (GRAVITY * dt);
		}
		float speed = p.vel.length();
		if (speed > MAX_SPEED)
			p.vel *= MAX_SPEED / speed;
		// Retire particles stuck in dead water - see STALL_SEC.
		p.stillTime = speed < STALL_SPEED ? p.stillTime + dt : 0.0f;
		if (p.stillTime > STALL_SEC)
			p.life = std::min(p.life, p.age + FADE_OUT_SEC);
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

	// One mesh of line segments, alpha fading from head to tail and
	// scaled by the particle's life envelope and speed. (A filled-quad
	// version with head squares was tried 2026-10-20 and reverted at the
	// user's request in favour of this look with longer trails.)
	trailMesh.setMode(OF_PRIMITIVE_LINES);
	int maxLen = (int)ofClamp((float)TRAIL_LENGTH, 2.0f, (float)MAX_TRAIL);
	float r = PARTICLE_COLOR[0], gC = PARTICLE_COLOR[1], b = PARTICLE_COLOR[2];
	for (const Particle & p : particles) {
		int n = std::min(p.trailCount, maxLen);
		if (!p.active || n < 2)
			continue;
		float envelope = std::min(1.0f, p.age / FADE_IN_SEC) * std::min(1.0f, (p.life - p.age) / FADE_OUT_SEC);
		float brightness = ofLerp(STILL_BRIGHTNESS, 1.0f, ofClamp(p.vel.length() / std::max(0.1f, FULL_BRIGHTNESS_SPEED), 0.0f, 1.0f));
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

void WaterFlowField::updateDewitz(float dt)
{
	const cv::Mat & elev = vegetationField->getElevationGrid();
	int cols = vegetationField->getCols(), rows = vegetationField->getRows();
	if (DEWITZ_SHOWN_ON == DEWITZ_SHOW_WATER_ONLY) {
		waterMask.resize((size_t)cols * rows);
		for (int y = 0; y < rows; y++)
			for (int x = 0; x < cols; x++)
				waterMask[(size_t)y * cols + x] = vegetationField->isWaterCell(x, y) ? 1 : 0;
		dewitz.setWaterMask(waterMask.data());
	} else {
		dewitz.setWaterMask(nullptr);
	}
	dewitz.update(elev.ptr<float>(0), cols, rows, vegetationField->getMmPerCell(), dt);

	// Projector-coordinate lookup on a coarse grid of fine cells.
	lutW = cols / PROJ_LUT_STEP + 2;
	lutH = rows / PROJ_LUT_STEP + 2;
	projLut.resize((size_t)lutW * lutH);
	for (int j = 0; j < lutH; j++)
		for (int i = 0; i < lutW; i++) {
			ofPoint p = gridToProjCoord((float)std::min(i * PROJ_LUT_STEP, cols - 1), (float)std::min(j * PROJ_LUT_STEP, rows - 1));
			projLut[(size_t)j * lutW + i].set(p.x, p.y);
		}

	dewitzPoints.clear();
	dewitzPoints.setMode(OF_PRIMITIVE_LINES);
	const std::vector<float> & X = dewitz.particleX();
	const std::vector<float> & Y = dewitz.particleY();
	const std::vector<float> & PX = dewitz.particlePrevX();
	const std::vector<float> & PY = dewitz.particlePrevY();
	const std::vector<float> & A = dewitz.particleAlpha();
	float baseAlpha = ofClamp(DEWITZ_POINT_ALPHA, 0.0f, 1.0f);
	for (size_t k = 0; k < X.size(); k++) {
		// Respawned this frame: prev == current, nothing to draw yet.
		if (PX[k] == X[k] && PY[k] == Y[k])
			continue;
		float a1 = baseAlpha * (k < A.size() ? A[k] : 1.0f); // fades on land
		if (a1 <= 0.0f)
			continue;
		ofFloatColor c(DEWITZ_COLOR[0], DEWITZ_COLOR[1], DEWITZ_COLOR[2], a1);
		ofVec2f a = fineToProj(PX[k], PY[k]);
		ofVec2f b = fineToProj(X[k], Y[k]);
		dewitzPoints.addVertex(ofVec3f(a.x, a.y, 0));
		dewitzPoints.addColor(c);
		dewitzPoints.addVertex(ofVec3f(b.x, b.y, 0));
		dewitzPoints.addColor(c);
	}

	obstacleMesh.clear();
	if (DEWITZ_SHOW_OBSTACLES) {
		obstacleMesh.setMode(OF_PRIMITIVE_TRIANGLES);
		int ds = std::max(1, DewitzFlow::DOWNSAMPLE);
		for (int j = 0; j < dewitz.latticeHeight(); j++)
			for (int i = 0; i < dewitz.latticeWidth(); i++) {
				if (!dewitz.isObstacle(i, j))
					continue;
				ofVec2f p00 = fineToProj((float)(i * ds), (float)(j * ds));
				ofVec2f p10 = fineToProj((float)((i + 1) * ds), (float)(j * ds));
				ofVec2f p01 = fineToProj((float)(i * ds), (float)((j + 1) * ds));
				ofVec2f p11 = fineToProj((float)((i + 1) * ds), (float)((j + 1) * ds));
				obstacleMesh.addVertex(ofVec3f(p00.x, p00.y, 0));
				obstacleMesh.addVertex(ofVec3f(p10.x, p10.y, 0));
				obstacleMesh.addVertex(ofVec3f(p11.x, p11.y, 0));
				obstacleMesh.addVertex(ofVec3f(p00.x, p00.y, 0));
				obstacleMesh.addVertex(ofVec3f(p11.x, p11.y, 0));
				obstacleMesh.addVertex(ofVec3f(p01.x, p01.y, 0));
			}
	}

	if (!dewitzFbo.isAllocated())
		return;
	dewitzFbo.begin();
	ofPushStyle();
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
	ofSetColor(0, 0, 0, (int)(ofClamp(DEWITZ_FADE, 0.0f, 1.0f) * 255));
	ofDrawRectangle(0, 0, dewitzFbo.getWidth(), dewitzFbo.getHeight());
	ofSetColor(255); // colour and alpha are per vertex (see above)
	dewitzPoints.draw();
	ofPopStyle();
	dewitzFbo.end();
}

ofVec2f WaterFlowField::fineToProj(float x, float y) const
{
	float fx = x / PROJ_LUT_STEP, fy = y / PROJ_LUT_STEP;
	int i0 = std::max(0, std::min((int)fx, lutW - 2)), j0 = std::max(0, std::min((int)fy, lutH - 2));
	float tx = fx - i0, ty = fy - j0;
	const ofVec2f & a = projLut[(size_t)j0 * lutW + i0];
	const ofVec2f & b = projLut[(size_t)j0 * lutW + i0 + 1];
	const ofVec2f & c = projLut[(size_t)(j0 + 1) * lutW + i0];
	const ofVec2f & d = projLut[(size_t)(j0 + 1) * lutW + i0 + 1];
	return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty;
}

void WaterFlowField::drawObstacleOverlay()
{
	if (!DEWITZ_SHOW_OBSTACLES || obstacleMesh.getNumVertices() == 0)
		return;
	ofPushStyle();
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
	ofSetColor(ofFloatColor(DEWITZ_OBSTACLE_COLOR[0], DEWITZ_OBSTACLE_COLOR[1],
		DEWITZ_OBSTACLE_COLOR[2], DEWITZ_OBSTACLE_COLOR[3]));
	obstacleMesh.draw();
	ofPopStyle();
}

void WaterFlowField::drawMainWindow(float x, float y, float width, float height)
{
	if (MOTION_MODE == MOTION_DEWITZ) {
		if (!dewitzFbo.isAllocated())
			return;
		if (projRes.x > 0 && projRes.y > 0) {
			ofPushMatrix();
			ofTranslate(x, y);
			ofScale(width / projRes.x, height / projRes.y);
			drawObstacleOverlay();
			ofPopMatrix();
		}
		ofPushStyle();
		ofEnableBlendMode(OF_BLENDMODE_ADD);
		ofSetColor(255);
		dewitzFbo.draw(x, y, width, height);
		ofPopStyle();
		return;
	}
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
	if (MOTION_MODE == MOTION_DEWITZ) {
		if (!dewitzFbo.isAllocated())
			return;
		drawObstacleOverlay();
		ofPushStyle();
		ofEnableBlendMode(OF_BLENDMODE_ADD); // black adds nothing
		ofSetColor(255);
		dewitzFbo.draw(0, 0);
		ofPopStyle();
		return;
	}
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
	ImGui::Text("Particles appear on visible water and run downhill along");
	ImGui::Text("the smoothed bed gradient, fading out at the shore.");
	ImGui::Text("Visual only - does not affect the simulation.");
	ImGui::Checkbox("Enable flow particles", &ENABLE_FLOW_FIELD);
	{
		static const char * masks[] = { "Colour map (visible blue)", "Simulated water" };
		static const char * motions[] = { "Bed slope", "Pipe-model velocity", "Lattice flow (LBM)", "Dewitz (2018)" };
		ImGui::Combo("Particles live on", &MASK_MODE, masks, 2);
		ImGui::Combo("Particles moved by", &MOTION_MODE, motions, 4);
	}
	if (MOTION_MODE == MOTION_DEWITZ) {
		drawDewitzGui();
		ImGui::End();
		return;
	}
	ImGui::Checkbox("Release particles at the source edge", &EMIT_AT_SOURCE);
	ImGui::SliderInt("Source band (cells)", &SOURCE_BAND_CELLS, 1, 40);
	ImGui::SliderFloat("Stall speed (cells/s)", &STALL_SPEED, 0.0f, 10.0f);
	ImGui::SliderFloat("Stall time before fading (s)", &STALL_SEC, 0.1f, 10.0f);
	ImGui::Text("Source cells: %d", (int)sourceCells.size());

	ImGui::Separator();
	ImGui::Text("Lattice flow (LBM) - how the water moves, solved inside the");
	ImGui::Text("pipe model's water: wet = fluid, dry sand = wall, driven in");
	ImGui::Text("from the source edge, draining out of the sink edge.");
	ImGui::Checkbox("Enable lattice flow", &LatticeFlow::ENABLE);
	ImGui::SliderInt("Lattice downsample (cells)", &LatticeFlow::DOWNSAMPLE, 2, 12);
	ImGui::SliderFloat("Inlet speed (lattice)", &LatticeFlow::INLET_SPEED, 0.0f, 0.2f);
	ImGui::SliderFloat("Relaxation tau (lower = more eddies)", &LatticeFlow::TAU, 0.51f, 2.0f);
	ImGui::SliderInt("Lattice steps per frame", &LatticeFlow::STEPS_PER_FRAME, 1, 8);
	ImGui::SliderFloat("Wet fraction for fluid", &LatticeFlow::WET_FRACTION, 0.1f, 1.0f);
	ImGui::Text("Fluid cells: %d, inlet cells: %d, cost: %.2f ms",
		lattice.getFluidCells(), lattice.getInletCells(), lattice.getLastMs());
	ImGui::Text("Max lattice speed: %.3f (keep below ~0.3), resets: %d",
		lattice.getMaxLatticeSpeed(), lattice.getResets());
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
	ImGui::SliderFloat("Life min (s)", &LIFE_MIN_SEC, 0.2f, 60.0f);
	ImGui::SliderFloat("Life max (s)", &LIFE_MAX_SEC, 0.2f, 60.0f);

	ImGui::Separator();
	ImGui::Text("Appearance");
	ImGui::SliderInt("Trail length (frames)", &TRAIL_LENGTH, 2, MAX_TRAIL);
	ImGui::SliderFloat("Trail thickness", &TRAIL_THICKNESS, 0.5f, 8.0f);
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

void WaterFlowField::drawDewitzGui()
{
	ImGui::Text("Dewitz et al. (2018), Sec. 3.3: whole-box lattice-Boltzmann flow,");
	ImGui::Text("hills/steep edges as walls, plus downhill and attractor pulls.");
	ImGui::Text("Visual only - the pipe-model hydrology is untouched.");
	ImGui::Text("(\"Particles live on\" above does not apply in this mode.)");

	ImGui::Separator();
	ImGui::Text("Lattice");
	ImGui::SliderInt("Downsample (fine cells per lattice cell)", &DewitzFlow::DOWNSAMPLE, 2, 4);
	ImGui::SliderFloat("Tau (Dewitz: 1.98)", &DewitzFlow::TAU, 0.51f, 2.5f);
	ImGui::Checkbox("MRT collision (off = BGK)", &DewitzFlow::USE_MRT);
	ImGui::SliderFloat("Inlet speed (lattice)", &DewitzFlow::INLET_SPEED, 0.0f, 0.15f);
	{
		static const char * edges[] = { "Left", "Right", "Top", "Bottom" };
		ImGui::Combo("Inlet edge (outlet opposite)", &DewitzFlow::INLET_EDGE, edges, 4);
	}
	ImGui::SliderInt("Lattice steps per frame", &DewitzFlow::STEPS_PER_FRAME, 1, 4);

	ImGui::Separator();
	ImGui::Text("Obstacles and gradient");
	ImGui::SliderFloat("Obstacle height (mm)", &DewitzFlow::OBSTACLE_HEIGHT_MM, 0.0f, 120.0f);
	ImGui::SliderFloat("Obstacle slope (rise/run)", &DewitzFlow::OBSTACLE_SLOPE, 0.1f, 3.0f);
	ImGui::SliderFloat("Slope walls only above (mm)", &DewitzFlow::OBSTACLE_BASE_MM, -60.0f, 60.0f);
	ImGui::SliderFloat("Slope-test blur (lattice cells)", &DewitzFlow::OBSTACLE_BLUR_SIGMA, 0.0f, 6.0f);
	ImGui::SliderFloat("Obstacle smoothing (s)", &DewitzFlow::OBSTACLE_SMOOTH_SEC, 0.0f, 2.0f);
	ImGui::SliderFloat("Terrain smoothing (s)", &DewitzFlow::TERRAIN_SMOOTH_SEC, 0.0f, 2.0f);
	ImGui::SliderFloat("Downhill saturation slope", &DewitzFlow::GRADIENT_SATURATION, 0.05f, 3.0f);

	ImGui::Separator();
	ImGui::Text("Particles: dir = W_FLOW*u/|u|max + W_GRAD*downhill + W_ATTR*attractors");
	ImGui::SliderFloat("W_FLOW", &DewitzFlow::W_FLOW, 0.0f, 3.0f);
	ImGui::SliderFloat("W_GRAD", &DewitzFlow::W_GRAD, 0.0f, 3.0f);
	ImGui::SliderFloat("W_ATTR", &DewitzFlow::W_ATTR, 0.0f, 3.0f);
	ImGui::SliderFloat("Speed (cells/s at |dir| = 1)", &DewitzFlow::SPEED, 0.0f, 400.0f);
	ImGui::SliderInt("Particle count", &DewitzFlow::PARTICLE_COUNT, 0, 100000);
	ImGui::SliderFloat("Particle life (s, 0 = until exit)", &DewitzFlow::PARTICLE_LIFE_SEC, 0.0f, 60.0f);

	ImGui::Separator();
	ImGui::Text("Attractors (grid cells; ArUco markers later)");
	ImGui::SliderFloat("Attractor radius (cells)", &DewitzFlow::ATTRACTOR_RADIUS, 1.0f, 100.0f);
	int cols = vegetationField ? vegetationField->getCols() : 0;
	int rows = vegetationField ? vegetationField->getRows() : 0;
	if (ImGui::Button("Add attractor at ROI centre") && cols > 0)
		dewitz.attractors.push_back({ cols * 0.5f, rows * 0.5f });
	for (size_t a = 0; a < dewitz.attractors.size(); a++) {
		ImGui::PushID((int)a);
		ImGui::SliderFloat("x", &dewitz.attractors[a].x, 0.0f, (float)std::max(1, cols));
		ImGui::SameLine();
		ImGui::SliderFloat("y", &dewitz.attractors[a].y, 0.0f, (float)std::max(1, rows));
		ImGui::SameLine();
		bool remove = ImGui::Button("Remove");
		ImGui::PopID();
		if (remove) {
			dewitz.attractors.erase(dewitz.attractors.begin() + a);
			break;
		}
	}

	ImGui::Separator();
	ImGui::Text("Drawing (prev->current line segments into a black FBO, composited additively)");
	ImGui::SliderFloat("Trail fade per frame", &DEWITZ_FADE, 0.0f, 0.5f);
	ImGui::SliderFloat("Particle alpha", &DEWITZ_POINT_ALPHA, 0.0f, 1.0f);
	ImGui::ColorEdit3("Particle colour", DEWITZ_COLOR);
	{
		static const char * shownOn[] = { "Water only", "Everywhere" };
		ImGui::Combo("Particles shown on", &DEWITZ_SHOWN_ON, shownOn, 2);
	}
	if (DEWITZ_SHOWN_ON == DEWITZ_SHOW_WATER_ONLY) {
		ImGui::SliderInt("Inlet spawn band (cells)", &DewitzFlow::SPAWN_BAND_CELLS, 1, 60);
		ImGui::SliderFloat("Fade-out on land (s)", &DewitzFlow::LAND_FADE_SEC, 0.05f, 3.0f);
		ImGui::Text("Water cells (simulated): %d, parked particles: %d",
			dewitz.getWaterCellsLastFrame(), dewitz.getParkedLastFrame());
	}
	ImGui::Checkbox("Show obstacle mask", &DEWITZ_SHOW_OBSTACLES);
	if (DEWITZ_SHOW_OBSTACLES)
		ImGui::ColorEdit4("Obstacle mask colour", DEWITZ_OBSTACLE_COLOR);

	ImGui::Separator();
	ImGui::Text("Lattice %dx%d, steps %lld, obstacle cells %d, wall flips last frame %d",
		dewitz.latticeWidth(), dewitz.latticeHeight(), dewitz.getSteps(), dewitz.getObstacleCells(), dewitz.getMaskFlips());
	ImGui::Text("Density resets %lld, non-finite %lld, max lattice speed %.3f",
		dewitz.getResets(), dewitz.getNonFinite(), dewitz.getMaxSpeed());
	ImGui::Text("Lattice %.2f ms, particles %.2f ms, exits %d, respawns %d (last frame)",
		dewitz.getLatticeMs(), dewitz.getParticleMs(), dewitz.getExitsLastFrame(), dewitz.getRespawnsLastFrame());
	if (cols > 0) {
		float fx, fy, gx, gy;
		dewitz.sampleTerms(cols * 0.5f, rows * 0.5f, fx, fy, gx, gy);
		ImGui::Text("ROI centre: flow (%.2f, %.2f), downhill (%.2f, %.2f)", fx, fy, gx, gy);
	}
}
