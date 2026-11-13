#include "MycorrhizaRenderer.h"
#include "../../KinectProjector/KinectProjector.h"
#include "../../Games/VegetationField.h"
#include "ofxImGui.h"

// Display defaults (DEVLOG, 2026-11-12; chosen on the harness, not yet on the sandbox).
bool MycorrhizaRenderer::OVERLAY_ON_SANDBOX = false;
float MycorrhizaRenderer::GAMMA = 0.9f;
float MycorrhizaRenderer::CORE_GAIN = 1.5f;
float MycorrhizaRenderer::GLOW_RADIUS = 2.0f;
float MycorrhizaRenderer::GLOW_STRENGTH = 0.25f;
float MycorrhizaRenderer::BRIGHTNESS = 1.0f;
float MycorrhizaRenderer::RAMP_POS[5] = { 0.0f, 0.35f, 0.65f, 0.9f, 1.0f };
// Deep indigo, violet, magenta, cyan, warm highlight (strongest strands
// only). No saturated green anywhere, so no red-green adjacency.
ofFloatColor MycorrhizaRenderer::RAMP_COLOR[5] = {
	ofFloatColor(0.10f, 0.04f, 0.32f),
	ofFloatColor(0.42f, 0.14f, 0.78f),
	ofFloatColor(0.88f, 0.22f, 0.78f),
	ofFloatColor(0.30f, 0.86f, 1.00f),
	ofFloatColor(1.00f, 0.80f, 0.50f) };

static const int LUT_STEP = 4;           // grid cells between mesh vertices
static const char * PRESET_FILE = "settings/mycorrhizaPreset.xml";
static const char * PRESET_NAME = "Prototype 2: Mycorrhiza";

void MycorrhizaRenderer::setup(std::shared_ptr<KinectProjector> const & kp, const VegetationField * vf)
{
	kinectProjector = kp;
	vegetation = vf;
#ifdef TARGET_OPENGLES
	shaderLoaded = false;
#else
	if (ofIsGLProgrammableRenderer())
		shaderLoaded = shader.load("shaders/mycorrhiza/GL3/mycorrhiza");
	else
		shaderLoaded = shader.load("shaders/mycorrhiza/GL2/mycorrhiza");
#endif
	if (!shaderLoaded)
		ofLogError("MycorrhizaRenderer") << "setup(): could not load shaders/mycorrhiza";
	if (ofFile::doesFileExist(PRESET_FILE))
		loadPreset();
	clear();
}

void MycorrhizaRenderer::setProjectorRes(const ofVec2f & res)
{
	projRes = res;
	fbo.allocate(std::max(1, (int)projRes.x), std::max(1, (int)projRes.y), GL_RGBA);
	fbo.begin();
	ofClear(0, 0, 0, 255);
	fbo.end();
}

void MycorrhizaRenderer::clear()
{
	field.clear();
	hasFrame = false;
	if (fbo.isAllocated()) {
		fbo.begin();
		ofClear(0, 0, 0, 255);
		fbo.end();
	}
}

void MycorrhizaRenderer::gatherInputs(MycorrhizaField::Inputs & in)
{
	const int cols = vegetation->getCols(), rows = vegetation->getRows();
	const cv::Mat & elev = vegetation->getElevationGrid();
	if (cols <= 0 || rows <= 0 || elev.rows != rows || elev.cols != cols || elev.type() != CV_32F || !elev.isContinuous())
		return;
	waterMask.resize((size_t)cols * rows);
	for (int gy = 0; gy < rows; gy++)
		for (int gx = 0; gx < cols; gx++)
			waterMask[(size_t)gy * cols + gx] = vegetation->isWaterCell(gx, gy) ? 1 : 0;
	in.elevationMM = elev.ptr<float>(0);
	in.water = waterMask.data();
	in.cols = cols;
	in.rows = rows;
	in.mmPerCell = vegetation->getMmPerCell();
	float dt = ofGetLastFrameTime();
	in.dt = (dt > 0.0f && dt <= 0.1f) ? dt : 1.0f / 60.0f;
	// Succession fields only when that model is running and sized to this grid.
	const SuccessionModel & sm = vegetation->getSuccession();
	if (VegetationField::VEGETATION_MODEL == VegetationField::MODEL_SITE_FEEDBACK && sm.getCols() == cols && sm.getRows() == rows) {
		for (int r = 0; r < 3; r++)
			in.cover[r] = sm.cover(r);
		in.soil = sm.soil();
		in.moisture = sm.moisture();
	}
}

void MycorrhizaRenderer::update()
{
	if (!kinectProjector || !vegetation)
		return;
	if (!kinectProjector->isImageStabilized()) {
		// No fresh depth: keep the network, re-adopt the surface when it returns.
		field.restartTouch();
		return;
	}
	MycorrhizaField::Inputs in;
	gatherInputs(in);
	if (!in.elevationMM)
		return;
	field.step(in);
	uploadTrace();
	rebuildMesh();
	renderFbo();
	hasFrame = true;
}

// 8-bit: round(sqrt(clamp(trace / level)) x 255) in R, G, B (the shader reads R).
void MycorrhizaRenderer::uploadTrace()
{
	const uint64_t t0 = ofGetElapsedTimeMicros();
	const int cols = field.getCols(), rows = field.getRows();
	if (!tracePix.isAllocated() || (int)tracePix.getWidth() != cols || (int)tracePix.getHeight() != rows)
		tracePix.allocate(cols, rows, OF_PIXELS_RGBA);
	if (!traceTex.isAllocated() || (int)traceTex.getWidth() != cols || (int)traceTex.getHeight() != rows) {
		traceTex.allocate(cols, rows, GL_RGBA8, true); // rectangle texture: texel coordinates
		traceTex.setTextureMinMagFilter(GL_LINEAR, GL_LINEAR);
		traceTex.setTextureWrap(GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
	}
	const float * t = field.trace();
	const float inv = 1.0f / std::max(1e-6f, field.getDisplayLevel());
	unsigned char * p = tracePix.getData();
	const size_t n = (size_t)cols * rows;
	for (size_t i = 0; i < n; i++) {
		const float v = std::min(1.0f, std::max(0.0f, t[i] * inv));
		const unsigned char e = (unsigned char)std::lround(std::sqrt(v) * 255.0f);
		p[4 * i + 0] = p[4 * i + 1] = p[4 * i + 2] = e;
		p[4 * i + 3] = 255;
	}
	traceTex.loadData(tracePix);
	lastUploadMs = (ofGetElapsedTimeMicros() - t0) / 1000.0f;
}

// Grid vertices every LUT_STEP cells at their projector positions
// (KinectProjector::kinectCoordToProjCoord, which depends on the current
// depth, so rebuilt each frame as SeedLayer/WaterFlowField do). Texcoord =
// trace texel coordinate of the cell centre.
void MycorrhizaRenderer::rebuildMesh()
{
	const uint64_t t0 = ofGetElapsedTimeMicros();
	const int cols = field.getCols(), rows = field.getRows();
	const int mw = (cols - 1 + LUT_STEP - 1) / LUT_STEP + 1, mh = (rows - 1 + LUT_STEP - 1) / LUT_STEP + 1;
	const ofVec2f origin = vegetation->getGridOrigin();
	const float step = vegetation->getGridStep();
	const bool rebuildIndices = mw != meshCols || mh != meshRows || mesh.getNumVertices() != (size_t)mw * mh;
	if (rebuildIndices) {
		mesh.clear();
		mesh.setMode(OF_PRIMITIVE_TRIANGLES);
		for (int j = 0; j < mh; j++)
			for (int i = 0; i < mw; i++) {
				mesh.addVertex(glm::vec3(0, 0, 0));
				mesh.addTexCoord(glm::vec2(0, 0));
			}
		for (int j = 0; j + 1 < mh; j++)
			for (int i = 0; i + 1 < mw; i++) {
				const ofIndexType a = j * mw + i, b = a + 1, c = a + mw, d = c + 1;
				mesh.addIndex(a); mesh.addIndex(b); mesh.addIndex(c);
				mesh.addIndex(b); mesh.addIndex(d); mesh.addIndex(c);
			}
		meshCols = mw;
		meshRows = mh;
	}
	for (int j = 0; j < mh; j++)
		for (int i = 0; i < mw; i++) {
			const float gx = (float)std::min(i * LUT_STEP, cols - 1), gy = (float)std::min(j * LUT_STEP, rows - 1);
			const ofVec2f p = kinectProjector->kinectCoordToProjCoord(origin.x + gx * step, origin.y + gy * step);
			const size_t v = (size_t)j * mw + i;
			mesh.setVertex(v, glm::vec3(p.x, p.y, 0));
			mesh.setTexCoord(v, glm::vec2(gx + 0.5f, gy + 0.5f));
		}
	lastMeshMs = (ofGetElapsedTimeMicros() - t0) / 1000.0f;
}

void MycorrhizaRenderer::renderFbo()
{
	if (!fbo.isAllocated())
		return;
	fbo.begin();
	ofClear(0, 0, 0, 255);
	if (shaderLoaded && traceTex.isAllocated()) {
		ofPushStyle();
		ofEnableBlendMode(OF_BLENDMODE_DISABLED);
		ofSetColor(255);
		shader.begin();
		shader.setUniformTexture("traceTex", traceTex, 0);
		shader.setUniform1f("gamma", std::max(0.05f, GAMMA));
		shader.setUniform1f("coreGain", CORE_GAIN);
		shader.setUniform1f("glowRadius", GLOW_RADIUS);
		shader.setUniform1f("glowStrength", GLOW_STRENGTH);
		shader.setUniform1f("brightness", BRIGHTNESS);
		float pos[5];
		float col[15];
		for (int s = 0; s < 5; s++) {
			pos[s] = s == 0 ? 0.0f : std::max(pos[s - 1], RAMP_POS[s]);
			col[3 * s + 0] = RAMP_COLOR[s].r;
			col[3 * s + 1] = RAMP_COLOR[s].g;
			col[3 * s + 2] = RAMP_COLOR[s].b;
		}
		shader.setUniform1fv("rampPos", pos, 5);
		shader.setUniform3fv("rampColor", col, 5);
		mesh.draw();
		shader.end();
		ofPopStyle();
		ofEnableBlendMode(OF_BLENDMODE_ALPHA);
	}
	fbo.end();
}

void MycorrhizaRenderer::composite(float x, float y, float w, float h, bool additive)
{
	if (!fbo.isAllocated())
		return;
	ofPushStyle();
	ofSetColor(255);
	if (additive) {
		if (!hasFrame) { ofPopStyle(); return; }
		ofEnableBlendMode(OF_BLENDMODE_ADD);
	} else {
		ofEnableBlendMode(OF_BLENDMODE_DISABLED);
	}
	fbo.draw(x, y, w, h);
	ofPopStyle();
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
}

void MycorrhizaRenderer::drawMainWindow(float x, float y, float w, float h) { composite(x, y, w, h, false); }
void MycorrhizaRenderer::drawProjectorWindow() { composite(0, 0, projRes.x, projRes.y, false); }
void MycorrhizaRenderer::drawOverlayMainWindow(float x, float y, float w, float h) { composite(x, y, w, h, true); }
void MycorrhizaRenderer::drawOverlayProjectorWindow() { composite(0, 0, projRes.x, projRes.y, true); }

// ------------------------------------------------------------ preset
#define MYC_FIELD_FLOATS(X) \
	X(TOUCH_MM) X(TOUCH_FAST_SEC) X(TOUCH_SLOW_SEC) X(TOUCH_RISE_PER_SEC) X(TOUCH_MEMORY_SEC) X(OCCLUSION_DROP) \
	X(SETTLE_SEC) X(SPAWN_PER_TOUCH) X(SENSOR_ANGLE_DEG) X(SENSOR_DIST_MM) X(ROTATE_DEG) X(STEP_MM) X(MIN_MOBILITY) \
	X(COMB_WEIGHT) X(COMB_SLOPE_SAT) X(CURL_SHARE) X(CURL_SCALE_MM) X(CURL_PERIOD_SEC) X(AGENT_DEPOSIT) \
	X(TRACE_DEPOSIT) X(TRACE_SENSE) X(TRACE_SATURATION) X(FOOD_DEPOSIT) X(TOUCH_DEPOSIT) X(TRAIL_DECAY) \
	X(TRACE_MEMORY_MIN) X(AGENT_LIFE_SEC) X(LEVEL_PERCENTILE) X(LEVEL_FALL_SEC) X(LEVEL_FLOOR)
#define MYC_FIELD_INTS(X) X(MAX_AGENTS) X(TOUCH_BLOCK) X(THREADS)
#define MYC_DISPLAY_FLOATS(X) X(GAMMA) X(CORE_GAIN) X(GLOW_RADIUS) X(GLOW_STRENGTH) X(BRIGHTNESS)

bool MycorrhizaRenderer::savePreset()
{
	ofXml xml;
	auto s = xml.appendChild("MYCORRHIZAPRESET");
	s.appendChild("name").set(std::string(PRESET_NAME));
#define SAVE_F(n) s.appendChild(#n).set(MycorrhizaField::n);
#define SAVE_D(n) s.appendChild(#n).set(n);
	MYC_FIELD_FLOATS(SAVE_F)
	MYC_FIELD_INTS(SAVE_F)
	MYC_DISPLAY_FLOATS(SAVE_D)
#undef SAVE_F
#undef SAVE_D
	for (int i = 0; i < 5; i++) {
		auto r = s.appendChild("ramp" + ofToString(i));
		r.appendChild("pos").set(RAMP_POS[i]);
		r.appendChild("r").set(RAMP_COLOR[i].r);
		r.appendChild("g").set(RAMP_COLOR[i].g);
		r.appendChild("b").set(RAMP_COLOR[i].b);
	}
	bool ok = xml.save(PRESET_FILE);
	presetStatus = ok ? std::string("saved \"") + PRESET_NAME + "\"" : "save failed";
	return ok;
}

// Reads only the entries present, so an older file keeps newer defaults.
bool MycorrhizaRenderer::loadPreset()
{
	ofXml xml;
	if (!xml.load(PRESET_FILE)) { presetStatus = "no preset file"; return false; }
	auto s = xml.getChild("MYCORRHIZAPRESET");
	if (!s) { presetStatus = "preset file not recognised"; return false; }
#define LOAD_F(n) if (s.getChild(#n)) MycorrhizaField::n = s.getChild(#n).getFloatValue();
#define LOAD_I(n) if (s.getChild(#n)) MycorrhizaField::n = s.getChild(#n).getIntValue();
#define LOAD_D(n) if (s.getChild(#n)) n = s.getChild(#n).getFloatValue();
	MYC_FIELD_FLOATS(LOAD_F)
	MYC_FIELD_INTS(LOAD_I)
	MYC_DISPLAY_FLOATS(LOAD_D)
#undef LOAD_F
#undef LOAD_I
#undef LOAD_D
	for (int i = 0; i < 5; i++) {
		auto r = s.getChild("ramp" + ofToString(i));
		if (!r) continue;
		if (r.getChild("pos")) RAMP_POS[i] = r.getChild("pos").getFloatValue();
		if (r.getChild("r")) RAMP_COLOR[i].r = r.getChild("r").getFloatValue();
		if (r.getChild("g")) RAMP_COLOR[i].g = r.getChild("g").getFloatValue();
		if (r.getChild("b")) RAMP_COLOR[i].b = r.getChild("b").getFloatValue();
	}
	presetStatus = std::string("loaded \"") + s.getChild("name").getValue() + "\"";
	return true;
}

// ------------------------------------------------------------ GUI
void MycorrhizaRenderer::drawGui()
{
	typedef MycorrhizaField F;
	ImGui::Begin("Mycorrhiza");
	ImGui::Text("Prototype 2: a trace of touch, drawn as a network.");
	ImGui::Text("Display only: reads the succession model, never writes it.");
	if (ImGui::Button("Clear network"))
		clear();
	ImGui::SameLine();
	if (ImGui::Button("Save preset"))
		savePreset();
	ImGui::SameLine();
	if (ImGui::Button("Load preset"))
		loadPreset();
	if (!presetStatus.empty())
		ImGui::Text("Preset: %s", presetStatus.c_str());
	ImGui::Checkbox("Overlay on sandbox (debug)", &OVERLAY_ON_SANDBOX);

	ImGui::Separator();
	ImGui::Text("fps %.1f (note the fps in each display mode)", ofGetFrameRate());
	ImGui::Text("Agents %d / %d, spawned %lld", field.agentCount(), F::MAX_AGENTS, field.spawnedTotal());
	ImGui::Text("Step %.2f ms (touch %.2f, comb %.2f, spawn %.2f, agents %.2f,", field.getLastStepMs(),
		field.getPhaseMs(0), field.getPhaseMs(1), field.getPhaseMs(2), field.getPhaseMs(3));
	ImGui::Text("  trail %.2f, trace %.2f, level %.2f); upload %.2f, mesh %.2f ms",
		field.getPhaseMs(4), field.getPhaseMs(5), field.getPhaseMs(6), lastUploadMs, lastMeshMs);
	ImGui::Text("Touched blocks %d; largest block change %.2f mm", field.touchedBlocks(), field.maxTouchChangeMM());
	ImGui::Text("(on untouched sand this must stay below the touch threshold)");
	ImGui::Text("Display level %.1f", field.getDisplayLevel());

	if (ImGui::CollapsingHeader("Touch", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderFloat("Touch threshold (mm)", &F::TOUCH_MM, 1.0f, 15.0f);
		ImGui::SliderFloat("Touch memory (s)", &F::TOUCH_MEMORY_SEC, 1.0f, 60.0f);
		ImGui::SliderFloat("Touch fast mean (s)", &F::TOUCH_FAST_SEC, 0.02f, 1.0f);
		ImGui::SliderFloat("Touch slow mean (s)", &F::TOUCH_SLOW_SEC, 0.2f, 10.0f);
		ImGui::SliderFloat("Touch rise (/s)", &F::TOUCH_RISE_PER_SEC, 0.5f, 20.0f);
		ImGui::SliderFloat("Hand occlusion drop", &F::OCCLUSION_DROP, 0.05f, 1.0f);
		ImGui::SliderInt("Touch block (cells)", &F::TOUCH_BLOCK, 2, 8);
	}
	if (ImGui::CollapsingHeader("Agents", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderInt("Agent cap", &F::MAX_AGENTS, 10000, 200000);
		ImGui::SliderFloat("Spawn per touch (/s/block)", &F::SPAWN_PER_TOUCH, 0.0f, 30.0f);
		ImGui::SliderFloat("Agent life (s)", &F::AGENT_LIFE_SEC, 2.0f, 60.0f);
		ImGui::SliderFloat("Sensor angle (deg)", &F::SENSOR_ANGLE_DEG, 5.0f, 90.0f);
		ImGui::SliderFloat("Sensor distance (mm)", &F::SENSOR_DIST_MM, 2.0f, 40.0f);
		ImGui::SliderFloat("Rotate (deg)", &F::ROTATE_DEG, 1.0f, 90.0f);
		ImGui::SliderFloat("Step (mm)", &F::STEP_MM, 0.1f, 4.0f);
		ImGui::SliderFloat("Min mobility (dry, bare)", &F::MIN_MOBILITY, 0.0f, 1.0f);
		ImGui::SliderFloat("Comb weight", &F::COMB_WEIGHT, 0.0f, 1.0f);
		ImGui::SliderFloat("Comb full at slope", &F::COMB_SLOPE_SAT, 0.02f, 0.6f);
		ImGui::SliderFloat("Curl share", &F::CURL_SHARE, 0.0f, 2.0f);
		ImGui::SliderFloat("Curl size (mm)", &F::CURL_SCALE_MM, 20.0f, 400.0f);
		ImGui::SliderFloat("Curl period (s)", &F::CURL_PERIOD_SEC, 5.0f, 200.0f);
		ImGui::SliderInt("Threads", &F::THREADS, 1, 8);
	}
	if (ImGui::CollapsingHeader("Trail and trace", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderFloat("Agent deposit", &F::AGENT_DEPOSIT, 0.0f, 10.0f);
		ImGui::SliderFloat("Trail decay (kept/step)", &F::TRAIL_DECAY, 0.5f, 0.99f);
		ImGui::SliderFloat("Food deposit (x cover)", &F::FOOD_DEPOSIT, 0.0f, 2.0f);
		ImGui::SliderFloat("Touch deposit (x touch)", &F::TOUCH_DEPOSIT, 0.0f, 1.0f);
		ImGui::SliderFloat("Trace deposit", &F::TRACE_DEPOSIT, 0.01f, 2.0f);
		ImGui::SliderFloat("Trace saturation", &F::TRACE_SATURATION, 5.0f, 200.0f);
		ImGui::SliderFloat("Trace sensed", &F::TRACE_SENSE, 0.0f, 1.0f);
		ImGui::SliderFloat("Trace memory (min)", &F::TRACE_MEMORY_MIN, 0.5f, 30.0f);
		ImGui::Text("(fades to about 10 %% in this many minutes)");
	}
	if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderFloat("Mycorrhiza brightness", &BRIGHTNESS, 0.2f, 3.0f);
		ImGui::SliderFloat("Gamma", &GAMMA, 0.2f, 2.0f);
		ImGui::SliderFloat("Core gain", &CORE_GAIN, 0.5f, 5.0f);
		ImGui::SliderFloat("Glow radius (cells)", &GLOW_RADIUS, 0.5f, 6.0f);
		ImGui::SliderFloat("Glow strength", &GLOW_STRENGTH, 0.0f, 2.0f);
		ImGui::SliderFloat("Level percentile", &F::LEVEL_PERCENTILE, 0.5f, 0.999f);
		ImGui::SliderFloat("Level floor", &F::LEVEL_FLOOR, 0.5f, 100.0f);
		ImGui::SliderFloat("Level fall (s)", &F::LEVEL_FALL_SEC, 10.0f, 3600.0f);
		static const char * names[5] = { "Ramp 0 (faint)", "Ramp 1", "Ramp 2", "Ramp 3", "Ramp 4 (strongest)" };
		for (int i = 0; i < 5; i++) {
			ImGui::PushID(i);
			ImGui::ColorEdit3(names[i], &RAMP_COLOR[i].r);
			if (i > 0 && i < 4)
				ImGui::SliderFloat("at", &RAMP_POS[i], 0.0f, 1.0f);
			ImGui::PopID();
		}
		ImGui::Text("(keep saturated red away from saturated green)");
	}
	ImGui::End();
}
