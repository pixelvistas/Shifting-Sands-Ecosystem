#include "SeedLayer.h"
#include "../KinectProjector/KinectProjector.h"
#include "ofxImGui.h"

float SeedLayer::SEED_SIZE_PX = 4.0f;
float SeedLayer::SEED_FADE = 0.125f; // half the former 0.25: longer trails
float SeedLayer::SEED_BRIGHTNESS = 1.0f;
float SeedLayer::SEED_OPACITY_ON_FILL = 0.7f;
bool SeedLayer::BAND_FILL_ON = true;

static const int POINT_SEGMENTS = 10; // triangle fan per soft point

void SeedLayer::setProjectorRes(const ofVec2f & res)
{
	projRes = res;
	// RGBA, premultiplied: alpha is the seeds' coverage, needed for the
	// "over" composite with band fill on.
	fbo.allocate(std::max(1, (int)projRes.x), std::max(1, (int)projRes.y), GL_RGBA);
	fbo.begin();
	ofClear(0, 0, 0, 0);
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

	// Soft round points: a fan from the centre (premultiplied colour x alpha,
	// alpha) to a rim of zero, so coverage falls off linearly to the edge.
	mesh.clear();
	mesh.setMode(OF_PRIMITIVE_TRIANGLES);
	const float bright = std::max(0.0f, SEED_BRIGHTNESS);
	const ofFloatColor rim(0, 0, 0, 0);
	float cs[POINT_SEGMENTS + 1], sn[POINT_SEGMENTS + 1];
	for (int k = 0; k <= POINT_SEGMENTS; k++) {
		float t = TWO_PI * k / POINT_SEGMENTS;
		cs[k] = std::cos(t); sn[k] = std::sin(t);
	}
	for (size_t i = 0; i < particles.count(); i++) {
		float a = particles.alpha(i);
		if (a <= 0.0f)
			continue;
		ofVec2f p = toProj(particles.posX(i), particles.posY(i));
		float r = 0.5f * SEED_SIZE_PX * particles.sizeFactor(i);
		if (r <= 0.0f)
			continue;
		const float * c = particles.rgb(i);
		ofFloatColor centre(std::min(1.0f, c[0] * bright) * a, std::min(1.0f, c[1] * bright) * a,
			std::min(1.0f, c[2] * bright) * a, a);
		for (int k = 0; k < POINT_SEGMENTS; k++) {
			mesh.addVertex(ofVec3f(p.x, p.y, 0));
			mesh.addColor(centre);
			mesh.addVertex(ofVec3f(p.x + r * cs[k], p.y + r * sn[k], 0));
			mesh.addColor(rim);
			mesh.addVertex(ofVec3f(p.x + r * cs[k + 1], p.y + r * sn[k + 1], 0));
			mesh.addColor(rim);
		}
	}

	if (!fbo.isAllocated())
		return;
	fbo.begin();
	ofPushStyle();
	ofEnableBlendMode(OF_BLENDMODE_DISABLED);
	glEnable(GL_BLEND);
	// Fade all four channels: dst *= (1 - fade).
	glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
	ofSetColor(0, 0, 0, (int)(ofClamp(SEED_FADE, 0.0f, 1.0f) * 255));
	ofDrawRectangle(0, 0, fbo.getWidth(), fbo.getHeight());
	// Seeds, premultiplied "over".
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	ofSetColor(255);
	mesh.draw();
	ofPopStyle();
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
	fbo.end();
}

// Band fill off: add the premultiplied colour (empty FBO adds nothing).
// Band fill on: premultiplied "over" at SEED_OPACITY_ON_FILL:
// sand x (1 - o x coverage) + o x colour.
void SeedLayer::composite(float x, float y, float w, float h)
{
	if (!drawnThisFrame())
		return;
	ofPushStyle();
	ofEnableBlendMode(OF_BLENDMODE_DISABLED);
	glEnable(GL_BLEND);
	if (BAND_FILL_ON) {
		float o = ofClamp(SEED_OPACITY_ON_FILL, 0.0f, 1.0f);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		ofSetColor(ofFloatColor(o, o, o, o));
	} else {
		glBlendFunc(GL_ONE, GL_ONE);
		ofSetColor(255);
	}
	fbo.draw(x, y, w, h);
	ofPopStyle();
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
}

void SeedLayer::drawMainWindow(float x, float y, float w, float h)
{
	composite(x, y, w, h);
}

void SeedLayer::drawProjectorWindow()
{
	composite(0, 0, projRes.x, projRes.y);
}

void SeedLayer::drawGui()
{
	ImGui::Checkbox("Seed particles", &SeedParticles::ENABLED);
	if (!SeedParticles::ENABLED)
		return;
	ImGui::Text("(seeds where cover is increasing; display only)");
	ImGui::Text("Seeds: %d, update %.2f ms", (int)particles.count(), particles.lastUpdateMs());
	ImGui::SliderFloat("Seeds per unit growth", &SeedParticles::SEEDS_PER_UNIT_GROWTH, 0.0f, 50.0f);
	ImGui::SliderFloat("Spawn percentile", &SeedParticles::SPAWN_PERCENTILE, 0.5f, 0.99f);
	ImGui::Text("(seeds only from the fastest-growing cells)");
	ImGui::Checkbox("Pioneer seeds only", &SeedParticles::PIONEER_ONLY);
	ImGui::SliderFloat("Growth window (s)", &SeedParticles::WINDOW_SEC, 0.1f, 2.0f);
	ImGui::SliderInt("Seed cap", &SeedParticles::MAX_PARTICLES, 100, 20000);
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
	ImGui::SliderFloat("Seed size (px)", &SEED_SIZE_PX, 1.0f, 10.0f);
	ImGui::SliderFloat("Seed trail length", &SEED_FADE, 0.01f, 0.5f);
	ImGui::Text("(fade per frame: lower = longer trails)");
	ImGui::SliderFloat("Seed opacity on fill", &SEED_OPACITY_ON_FILL, 0.0f, 1.0f);
	ImGui::SliderFloat("Seed brightness", &SEED_BRIGHTNESS, 0.5f, 3.0f);
	ImGui::Checkbox("Mortality motes", &SeedParticles::MORTALITY_MOTES);
	if (SeedParticles::MORTALITY_MOTES)
		ImGui::SliderFloat("Motes per unit loss", &SeedParticles::MOTES_PER_UNIT_LOSS, 0.0f, 200.0f);
}
