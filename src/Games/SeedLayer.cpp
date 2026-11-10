#include "SeedLayer.h"
#include "../KinectProjector/KinectProjector.h"
#include "ofxImGui.h"

float SeedLayer::SEED_SIZE_PX = 2.0f;
float SeedLayer::SEED_FADE = 0.25f;
float SeedLayer::SEED_BRIGHTNESS = 1.0f;

void SeedLayer::setProjectorRes(const ofVec2f & res)
{
	projRes = res;
	// GL_RGB, no alpha channel (see WaterFlowField::setProjectorRes).
	fbo.allocate(std::max(1, (int)projRes.x), std::max(1, (int)projRes.y), GL_RGB);
	fbo.begin();
	ofClear(0, 0, 0, 255);
	fbo.end();
}

ofVec2f SeedLayer::toProj(float gx, float gy) const
{
	float fx = gx / LUT_STEP, fy = gy / LUT_STEP;
	int i0 = std::max(0, std::min((int)fx, lutW - 2)), j0 = std::max(0, std::min((int)fy, lutH - 2));
	float tx = fx - i0, ty = fy - j0;
	const ofVec2f & a = lut[(size_t)j0 * lutW + i0];
	const ofVec2f & b = lut[(size_t)j0 * lutW + i0 + 1];
	const ofVec2f & c = lut[(size_t)(j0 + 1) * lutW + i0];
	const ofVec2f & d = lut[(size_t)(j0 + 1) * lutW + i0 + 1];
	return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty;
}

bool SeedLayer::drawnThisFrame() const
{
	// Not updated this frame (other vegetation model, seeds off): draw
	// nothing rather than a stale FBO.
	// (<= 1 frame old: the projector window may draw after the frame count ticks.)
	return SeedParticles::ENABLED && ofGetFrameNum() - updatedFrame <= 1 && fbo.isAllocated();
}

void SeedLayer::update(std::shared_ptr<KinectProjector> const & kp, const float * const cover[3], const float * soil,
	const unsigned char * water, int cols, int rows, float mmPerCell, float dt, int interleave,
	ofVec2f origin, float step)
{
	particles.update(cover, soil, water, cols, rows, mmPerCell, dt, interleave);
	if (!SeedParticles::ENABLED || !kp || cols <= 0 || rows <= 0)
		return;
	updatedFrame = ofGetFrameNum();

	// Grid -> projector lookup (cell coordinate gx -> kinect origin + gx * step),
	// rebuilt each frame like WaterFlowField's (it depends on the depth).
	lutW = cols / LUT_STEP + 2;
	lutH = rows / LUT_STEP + 2;
	lut.resize((size_t)lutW * lutH);
	for (int j = 0; j < lutH; j++)
		for (int i = 0; i < lutW; i++) {
			float gx = (float)std::min(i * LUT_STEP, cols), gy = (float)std::min(j * LUT_STEP, rows);
			lut[(size_t)j * lutW + i] = kp->kinectCoordToProjCoord(origin.x + gx * step, origin.y + gy * step);
		}

	mesh.clear();
	mesh.setMode(OF_PRIMITIVE_TRIANGLES);
	const float bright = std::max(0.0f, SEED_BRIGHTNESS);
	for (size_t i = 0; i < particles.count(); i++) {
		float a = particles.alpha(i);
		if (a <= 0.0f)
			continue;
		ofVec2f p = toProj(particles.posX(i), particles.posY(i));
		float h = 0.5f * SEED_SIZE_PX * particles.sizeFactor(i);
		const float * c = particles.rgb(i);
		ofFloatColor col(std::min(1.0f, c[0] * bright), std::min(1.0f, c[1] * bright), std::min(1.0f, c[2] * bright), a);
		ofVec3f v00(p.x - h, p.y - h, 0), v10(p.x + h, p.y - h, 0), v01(p.x - h, p.y + h, 0), v11(p.x + h, p.y + h, 0);
		mesh.addVertex(v00); mesh.addVertex(v10); mesh.addVertex(v11);
		mesh.addVertex(v00); mesh.addVertex(v11); mesh.addVertex(v01);
		for (int k = 0; k < 6; k++) mesh.addColor(col);
	}

	if (!fbo.isAllocated())
		return;
	fbo.begin();
	ofPushStyle();
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
	ofSetColor(0, 0, 0, (int)(ofClamp(SEED_FADE, 0.0f, 1.0f) * 255));
	ofDrawRectangle(0, 0, fbo.getWidth(), fbo.getHeight());
	ofSetColor(255); // colour and alpha per vertex
	mesh.draw();
	ofPopStyle();
	fbo.end();
}

void SeedLayer::drawMainWindow(float x, float y, float w, float h)
{
	if (!drawnThisFrame())
		return;
	ofPushStyle();
	ofEnableBlendMode(OF_BLENDMODE_ADD);
	ofSetColor(255);
	fbo.draw(x, y, w, h);
	ofPopStyle();
}

void SeedLayer::drawProjectorWindow()
{
	if (!drawnThisFrame())
		return;
	ofPushStyle();
	ofEnableBlendMode(OF_BLENDMODE_ADD); // black adds nothing
	ofSetColor(255);
	fbo.draw(0, 0);
	ofPopStyle();
}

void SeedLayer::drawGui()
{
	ImGui::Checkbox("Seed particles", &SeedParticles::ENABLED);
	if (!SeedParticles::ENABLED)
		return;
	ImGui::Text("(seeds where cover is increasing; display only)");
	ImGui::Text("Seeds: %d, update %.2f ms", (int)particles.count(), particles.lastUpdateMs());
	ImGui::SliderFloat("Seeds per unit growth", &SeedParticles::SEEDS_PER_UNIT_GROWTH, 0.0f, 200.0f);
	ImGui::SliderFloat("Growth window (s)", &SeedParticles::WINDOW_SEC, 0.1f, 2.0f);
	ImGui::SliderInt("Seed cap", &SeedParticles::MAX_PARTICLES, 1000, 50000);
	ImGui::SliderFloat("Wind direction (deg)", &SeedParticles::WIND_DIR_DEG, 0.0f, 360.0f);
	ImGui::SliderFloat("Wind (mm/s)", &SeedParticles::WIND_MM_PER_SEC, 0.0f, 30.0f);
	ImGui::SliderFloat("Eddies (mm/s)", &SeedParticles::CURL_MM_PER_SEC, 0.0f, 20.0f);
	ImGui::SliderFloat("Eddy size (mm)", &SeedParticles::CURL_SCALE_MM, 10.0f, 200.0f);
	ImGui::SliderFloat("Eddy period (s)", &SeedParticles::CURL_PERIOD_SEC, 2.0f, 60.0f);
	ImGui::SliderFloat("Drift from front (mm/s)", &SeedParticles::OUTWARD_MM_PER_SEC, 0.0f, 20.0f);
	ImGui::SliderFloat("Speed, pioneer", &SeedParticles::SPEED[SeedParticles::PIONEER], 0.0f, 2.0f);
	ImGui::SliderFloat("Speed, intermediate", &SeedParticles::SPEED[SeedParticles::INTERMEDIATE], 0.0f, 2.0f);
	ImGui::SliderFloat("Speed, climax", &SeedParticles::SPEED[SeedParticles::CLIMAX], 0.0f, 2.0f);
	ImGui::SliderFloat("Life, pioneer (s)", &SeedParticles::LIFE_SEC[SeedParticles::PIONEER], 0.2f, 15.0f);
	ImGui::SliderFloat("Life, intermediate (s)", &SeedParticles::LIFE_SEC[SeedParticles::INTERMEDIATE], 0.2f, 15.0f);
	ImGui::SliderFloat("Life, climax (s)", &SeedParticles::LIFE_SEC[SeedParticles::CLIMAX], 0.2f, 15.0f);
	ImGui::SliderFloat("Seed size (px)", &SEED_SIZE_PX, 1.0f, 6.0f);
	ImGui::SliderFloat("Seed trail fade", &SEED_FADE, 0.02f, 1.0f);
	ImGui::SliderFloat("Seed brightness", &SEED_BRIGHTNESS, 0.5f, 3.0f);
	ImGui::Checkbox("Mortality motes", &SeedParticles::MORTALITY_MOTES);
	if (SeedParticles::MORTALITY_MOTES)
		ImGui::SliderFloat("Motes per unit loss", &SeedParticles::MOTES_PER_UNIT_LOSS, 0.0f, 200.0f);
}
