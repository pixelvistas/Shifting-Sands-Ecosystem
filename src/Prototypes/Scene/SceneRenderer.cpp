#include "SceneRenderer.h"
#include "../../KinectProjector/KinectProjector.h"
#include "../../Games/VegetationField.h"
#include "ofxImGui.h"
#include <cstring>

static ofFloatColor hexColor(unsigned int h)
{
	return ofFloatColor(((h >> 16) & 0xff) / 255.0f, ((h >> 8) & 0xff) / 255.0f, (h & 0xff) / 255.0f);
}

// Defaults (DEVLOG, 2026-11-18; chosen on the headless preview against the
// two reference clips, not yet on the sand).
int SceneRenderer::SCENE_MULT = 4;
bool SceneRenderer::FREEZE = false;
float SceneRenderer::HEIGHT_LO_MM = -40.0f;
float SceneRenderer::HEIGHT_HI_MM = 80.0f;
float SceneRenderer::TERRAIN_MID_POS = 0.45f;
ofFloatColor SceneRenderer::TERRAIN_LOW = hexColor(0x0B3D2E);   // deep emerald
ofFloatColor SceneRenderer::TERRAIN_MID = hexColor(0x3B5A34);
ofFloatColor SceneRenderer::TERRAIN_HIGH = hexColor(0xC9A26B);  // warm sand
float SceneRenderer::EXAGGERATION = 1.5f;
float SceneRenderer::SUN_AZIMUTH_DEG = 225.0f;                 // from the upper left (grid y down)
float SceneRenderer::SUN_ELEVATION_DEG = 40.0f;
ofFloatColor SceneRenderer::SUN_COLOR = hexColor(0xFFE9C4);
float SceneRenderer::AMBIENT = 0.3f;
float SceneRenderer::DAPPLE_SCALE_MM = 110.0f;
float SceneRenderer::DAPPLED_LIGHT_SPEED = 6.0f;               // mm/s
float SceneRenderer::DAPPLE_DIRECTION_DEG = 30.0f;
float SceneRenderer::DAPPLE_CONTRAST = 0.55f;
float SceneRenderer::DAPPLE_SHADE = 0.3f;
float SceneRenderer::BLADE_LENGTH_MM = 5.0f;
float SceneRenderer::BLADE_WIDTH_MM = 1.0f;
float SceneRenderer::BLADES_PER_TUFT = 5.0f;
float SceneRenderer::GRASS_MIN_MM = -6.0f;
float SceneRenderer::GRASS_MAX_MM = 45.0f;
float SceneRenderer::GRASS_MAX_SLOPE = 0.9f;
float SceneRenderer::CLUMP_SCALE_MM = 60.0f;
float SceneRenderer::CLUMP_AMOUNT = 0.6f;
ofFloatColor SceneRenderer::GRASS_BASE = hexColor(0x1C6A2B);
ofFloatColor SceneRenderer::GRASS_TIP = hexColor(0x7FD45C);
float SceneRenderer::TIP_BOOST = 1.6f;
float SceneRenderer::WIND_DIRECTION_DEG = 80.0f;
float SceneRenderer::WIND_STRENGTH = 0.35f;
float SceneRenderer::WIND_WAVELENGTH_MM = 150.0f;
float SceneRenderer::WIND_SPEED = 0.25f;                       // waves per second
float SceneRenderer::GUST_STRENGTH = 0.8f;
float SceneRenderer::LEAN_SPREAD_DEG = 35.0f;
float SceneRenderer::WATER_EDGE_MM = 2.0f;
float SceneRenderer::WATER_DEEP_MM = 30.0f;
ofFloatColor SceneRenderer::WATER_SHALLOW = hexColor(0x0E3B40);
ofFloatColor SceneRenderer::WATER_DEEP = hexColor(0x02070A);
float SceneRenderer::RIPPLE_NORMAL_GAIN = 4.0f;
float SceneRenderer::SPEC_STRENGTH = 1.2f;
float SceneRenderer::SHININESS = 60.0f;
float SceneRenderer::RIPPLE_SHIMMER = 0.6f;
float SceneRenderer::HIGHLIGHT_MAX = 0.5f;  // water glints never above this (no white-out)
float SceneRenderer::REFLECT_STRENGTH = 0.12f;
float SceneRenderer::REFLECT_DIRECTION_DEG = 90.0f;
float SceneRenderer::REFLECT_DISTANCE_MM = 25.0f;
float SceneRenderer::REFLECT_AMP_MM = 2.0f;
float SceneRenderer::REFLECT_FREQ = 0.15f;
float SceneRenderer::REFLECT_SPEED = 1.5f;
float SceneRenderer::BLACK_POINT = 0.04f;
float SceneRenderer::BLACK_SOFTNESS = 0.03f;

static const int LUT_STEP = 4;
static const char * PRESET_FILE = "settings/scenePreset.xml";
static const char * PRESET_NAME = "Prototype 4: Scene";

void SceneRenderer::setup(std::shared_ptr<KinectProjector> const & kp, const VegetationField * vf)
{
	kinectProjector = kp;
	vegetation = vf;
	if (ofFile::doesFileExist(PRESET_FILE))
		loadPreset();
	const size_t n = std::min(SceneStylizer::RAMP.size(), sizeof(rampBuf) - 1);
	std::memcpy(rampBuf, SceneStylizer::RAMP.data(), n);
	rampBuf[n] = '\0';
}

void SceneRenderer::setProjectorRes(const ofVec2f & res)
{
	projRes = res;
	stylizer.setProjectorRes(res);
}

// Projector pixels per mm on the sand (as Prototype 3's AsciiRenderer).
void SceneRenderer::measureScale()
{
	const int cols = vegetation->getCols(), rows = vegetation->getRows();
	const float step = vegetation->getGridStep();
	const float mmPerKinectPx = vegetation->getMmPerCell() / std::max(1e-6f, step);
	const ofVec2f o = vegetation->getGridOrigin();
	const float w = cols * step, h = rows * step;
	if (cols < 4 || rows < 4 || mmPerKinectPx <= 0.0f)
		return;
	const ofVec2f a = kinectProjector->kinectCoordToProjCoord(o.x + 0.25f * w, o.y + 0.5f * h);
	const ofVec2f b = kinectProjector->kinectCoordToProjCoord(o.x + 0.75f * w, o.y + 0.5f * h);
	const ofVec2f c = kinectProjector->kinectCoordToProjCoord(o.x + 0.5f * w, o.y + 0.25f * h);
	const ofVec2f d = kinectProjector->kinectCoordToProjCoord(o.x + 0.5f * w, o.y + 0.75f * h);
	const float sx = a.distance(b) / (0.5f * w * mmPerKinectPx);
	const float sy = c.distance(d) / (0.5f * h * mmPerKinectPx);
	if (!(sx > 0.0f) || !(sy > 0.0f) || !std::isfinite(sx) || !std::isfinite(sy))
		return;
	pxPerMM = 0.5f * (sx + sy);
	scaleMeasured = true;
	measuredOrigin = o;
	measuredCols = cols;
	measuredRows = rows;
}

// Grid vertices every LUT_STEP cells at their projector positions (depends
// on the current depth, so rebuilt each frame); texcoord = scene texel
// coordinate of the cell centre.
void SceneRenderer::rebuildMesh()
{
	const uint64_t t0 = ofGetElapsedTimeMicros();
	const int cols = vegetation->getCols(), rows = vegetation->getRows();
	const int mw = (cols - 1 + LUT_STEP - 1) / LUT_STEP + 1, mh = (rows - 1 + LUT_STEP - 1) / LUT_STEP + 1;
	const ofVec2f origin = vegetation->getGridOrigin();
	const float step = vegetation->getGridStep();
	const int m = std::max(1, std::min(6, SCENE_MULT));
	if (mw != meshCols || mh != meshRows || mesh.getNumVertices() != (size_t)mw * mh) {
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
			const ofVec2f pr = kinectProjector->kinectCoordToProjCoord(origin.x + gx * step, origin.y + gy * step);
			const size_t v = (size_t)j * mw + i;
			mesh.setVertex(v, glm::vec3(pr.x, pr.y, 0));
			mesh.setTexCoord(v, glm::vec2((gx + 0.5f) * m, (gy + 0.5f) * m));
		}
	meshMult = m;
	cpuMeshMs = (ofGetElapsedTimeMicros() - t0) / 1000.0f;
}

void SceneRenderer::update()
{
	if (!kinectProjector || !vegetation || glFailed)
		return;
	const int cols = vegetation->getCols(), rows = vegetation->getRows();
	const cv::Mat & elev = vegetation->getElevationGrid();
	if (cols <= 0 || rows <= 0 || elev.rows != rows || elev.cols != cols || elev.type() != CV_32F)
		return;
	if (!glReady) {
		glReady = SceneGL::load(sceneShader, "sceneRender") && SceneGL::load(warpShader, "sceneWarp")
			&& input.setup() && ripples.setup() && stylizer.setup();
		glFailed = !glReady;
		if (glFailed)
			return;
	}
	const uint64_t frame = ofGetFrameNum();
	if (frame != lastUpdateFrame + 1) { // (re-)entering the mode: start from the current sand
		input.reset();
		ripples.reset();
	}
	lastUpdateFrame = frame;
	frameCounter++;
	float dt = ofGetLastFrameTime();
	if (!(dt > 0.0f) || dt > 0.1f)
		dt = 1.0f / 60.0f;
	if (!FREEZE)
		animTime += dt;

	if (!scaleMeasured || measuredCols != cols || measuredRows != rows || measuredOrigin != vegetation->getGridOrigin())
		measureScale();
	if (!scaleMeasured)
		return;
	SceneStylizer::CELL_MM = ofClamp(SceneStylizer::CELL_MM, 2.0f, 60.0f);
	stylizer.setCellHeightPx((int)std::lround(SceneStylizer::CELL_MM * pxPerMM));

	tInput.begin();
	input.update(*vegetation, dt, animTime, frameCounter);
	tInput.end();
	cpuInputMs = input.getLastCpuMs();
	tRipple.begin();
	ripples.update(input.getHeight(), input.getWater(), cols, rows, vegetation->getMmPerCell(), dt, !FREEZE, frameCounter);
	tRipple.end();
	rebuildMesh();
	tScene.begin();
	renderScene();
	tScene.end();
	tWarp.begin();
	warpScene();
	tWarp.end();
	tStyle.begin();
	stylizer.render(warpedFbo.getTexture(), BLACK_POINT);
	tStyle.end();
	hasFrame = true;
}

void SceneRenderer::renderScene()
{
	const int cols = vegetation->getCols(), rows = vegetation->getRows();
	const int m = std::max(1, std::min(6, SCENE_MULT));
	if (cols * m != sceneCols || rows * m != sceneRows || !sceneFbo.isAllocated()) {
		sceneCols = cols * m;
		sceneRows = rows * m;
		SceneGL::allocate(sceneFbo, sceneCols, sceneRows, GL_RGBA16F, GL_LINEAR);
		sceneQuad = SceneGL::quad((float)sceneCols, (float)sceneRows);
	}
	const float az = ofDegToRad(SUN_AZIMUTH_DEG), el = ofDegToRad(ofClamp(SUN_ELEVATION_DEG, 1.0f, 90.0f));
	const glm::vec3 sun(std::cos(az) * std::cos(el), std::sin(az) * std::cos(el), std::sin(el));
	const float da = ofDegToRad(DAPPLE_DIRECTION_DEG), wa = ofDegToRad(WIND_DIRECTION_DEG), ra = ofDegToRad(REFLECT_DIRECTION_DEG);
	ofShader & s = sceneShader;
	ofPushStyle();
	ofEnableBlendMode(OF_BLENDMODE_DISABLED);
	ofSetColor(255);
	sceneFbo.begin();
	ofClear(0, 0, 0, 255);
	s.begin();
	s.setUniformTexture("heightTex", input.getHeight(), 0);
	s.setUniformTexture("waterTex", input.getWater(), 1);
	s.setUniformTexture("rippleTex", ripples.getTexture(), 2);
	s.setUniform1f("sceneMult", (float)m);
	s.setUniform1f("rippleMult", (float)std::max(1, ripples.getMult()));
	s.setUniform2f("gridSize", (float)cols, (float)rows);
	s.setUniform1f("mmPerCell", vegetation->getMmPerCell());
	s.setUniform1f("animTime", animTime);
	s.setUniform1f("heightLo", HEIGHT_LO_MM);
	s.setUniform1f("heightHi", HEIGHT_HI_MM);
	s.setUniform3f("terrainLow", TERRAIN_LOW.r, TERRAIN_LOW.g, TERRAIN_LOW.b);
	s.setUniform3f("terrainMid", TERRAIN_MID.r, TERRAIN_MID.g, TERRAIN_MID.b);
	s.setUniform3f("terrainHigh", TERRAIN_HIGH.r, TERRAIN_HIGH.g, TERRAIN_HIGH.b);
	s.setUniform1f("terrainMidPos", ofClamp(TERRAIN_MID_POS, 0.01f, 0.99f));
	s.setUniform1f("exaggeration", EXAGGERATION);
	s.setUniform3f("sunDir", sun.x, sun.y, sun.z);
	s.setUniform3f("sunColor", SUN_COLOR.r, SUN_COLOR.g, SUN_COLOR.b);
	s.setUniform1f("ambient", AMBIENT);
	s.setUniform1f("dappleScale", DAPPLE_SCALE_MM);
	s.setUniform2f("dappleVelocity", std::cos(da) * DAPPLED_LIGHT_SPEED, std::sin(da) * DAPPLED_LIGHT_SPEED);
	s.setUniform1f("dappleContrast", ofClamp(DAPPLE_CONTRAST, 0.01f, 1.0f));
	s.setUniform1f("dappleShade", ofClamp(DAPPLE_SHADE, 0.0f, 1.0f));
	s.setUniform1f("bladeLength", BLADE_LENGTH_MM);
	s.setUniform1f("bladeWidth", BLADE_WIDTH_MM);
	s.setUniform1f("bladesPerTuft", ofClamp(BLADES_PER_TUFT, 0.0f, 6.0f));
	s.setUniform1f("grassMinHeight", GRASS_MIN_MM);
	s.setUniform1f("grassMaxHeight", GRASS_MAX_MM);
	s.setUniform1f("grassMaxSlope", std::max(0.01f, GRASS_MAX_SLOPE));
	s.setUniform1f("clumpScale", CLUMP_SCALE_MM);
	s.setUniform1f("clumpAmount", ofClamp(CLUMP_AMOUNT, 0.0f, 1.0f));
	s.setUniform3f("grassBase", GRASS_BASE.r, GRASS_BASE.g, GRASS_BASE.b);
	s.setUniform3f("grassTip", GRASS_TIP.r, GRASS_TIP.g, GRASS_TIP.b);
	s.setUniform1f("tipBoost", TIP_BOOST);
	s.setUniform2f("windDir", std::cos(wa), std::sin(wa));
	s.setUniform1f("windStrength", WIND_STRENGTH);
	s.setUniform1f("windWaveLength", WIND_WAVELENGTH_MM);
	s.setUniform1f("windSpeed", WIND_SPEED);
	s.setUniform1f("gustStrength", GUST_STRENGTH);
	s.setUniform1f("leanSpread", ofDegToRad(LEAN_SPREAD_DEG));
	s.setUniform1f("waterThreshold", SceneHeightInput::WATER_THRESHOLD_MM);
	s.setUniform1f("waterEdge", WATER_EDGE_MM);
	s.setUniform1f("waterDeepDepth", WATER_DEEP_MM);
	s.setUniform3f("waterShallow", WATER_SHALLOW.r, WATER_SHALLOW.g, WATER_SHALLOW.b);
	s.setUniform3f("waterDeep", WATER_DEEP.r, WATER_DEEP.g, WATER_DEEP.b);
	s.setUniform1f("rippleNormalGain", RIPPLE_NORMAL_GAIN);
	s.setUniform1f("specStrength", SPEC_STRENGTH);
	s.setUniform1f("shininess", SHININESS);
	s.setUniform1f("rippleShimmer", RIPPLE_SHIMMER);
	s.setUniform1f("highlightMax", std::max(0.01f, HIGHLIGHT_MAX));
	s.setUniform1f("reflectStrength", REFLECT_STRENGTH);
	s.setUniform2f("reflectOffset", std::cos(ra) * REFLECT_DISTANCE_MM, std::sin(ra) * REFLECT_DISTANCE_MM);
	s.setUniform1f("reflectAmp", REFLECT_AMP_MM);
	s.setUniform1f("reflectFreq", REFLECT_FREQ);
	s.setUniform1f("reflectSpeed", REFLECT_SPEED);
	s.setUniform1f("blackPoint", BLACK_POINT);
	s.setUniform1f("blackSoftness", std::max(0.0f, BLACK_SOFTNESS));
	sceneQuad.draw();
	s.end();
	sceneFbo.end();
	ofPopStyle();
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
}

void SceneRenderer::warpScene()
{
	if (!warpedFbo.isAllocated() || (int)warpedFbo.getWidth() != (int)projRes.x || (int)warpedFbo.getHeight() != (int)projRes.y)
		SceneGL::allocate(warpedFbo, (int)projRes.x, (int)projRes.y, GL_RGBA16F, GL_NEAREST);
	ofPushStyle();
	ofEnableBlendMode(OF_BLENDMODE_DISABLED);
	ofSetColor(255);
	warpedFbo.begin();
	ofClear(0, 0, 0, 255);
	warpShader.begin();
	warpShader.setUniformTexture("sceneTex", sceneFbo.getTexture(), 0);
	mesh.draw();
	warpShader.end();
	warpedFbo.end();
	ofPopStyle();
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
}

void SceneRenderer::drawMainWindow(float x, float y, float w, float h)
{
	ofPushStyle();
	ofEnableBlendMode(OF_BLENDMODE_DISABLED);
	if (hasFrame && stylizer.getOutput().isAllocated()) {
		ofSetColor(255);
		stylizer.getOutput().draw(x, y, w, h);
	} else {
		ofSetColor(0);
		ofDrawRectangle(x, y, w, h);
	}
	ofPopStyle();
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
}

void SceneRenderer::drawProjectorWindow() { drawMainWindow(0, 0, projRes.x, projRes.y); }

// ------------------------------------------------------------ preset
#define SCENE_FLOATS(X) \
	X(SceneRenderer, HEIGHT_LO_MM) X(SceneRenderer, HEIGHT_HI_MM) X(SceneRenderer, TERRAIN_MID_POS) X(SceneRenderer, EXAGGERATION) \
	X(SceneRenderer, SUN_AZIMUTH_DEG) X(SceneRenderer, SUN_ELEVATION_DEG) X(SceneRenderer, AMBIENT) \
	X(SceneRenderer, DAPPLE_SCALE_MM) X(SceneRenderer, DAPPLED_LIGHT_SPEED) X(SceneRenderer, DAPPLE_DIRECTION_DEG) \
	X(SceneRenderer, DAPPLE_CONTRAST) X(SceneRenderer, DAPPLE_SHADE) X(SceneRenderer, BLADE_LENGTH_MM) X(SceneRenderer, BLADE_WIDTH_MM) \
	X(SceneRenderer, BLADES_PER_TUFT) X(SceneRenderer, GRASS_MIN_MM) X(SceneRenderer, GRASS_MAX_MM) X(SceneRenderer, GRASS_MAX_SLOPE) \
	X(SceneRenderer, CLUMP_SCALE_MM) X(SceneRenderer, CLUMP_AMOUNT) X(SceneRenderer, TIP_BOOST) X(SceneRenderer, WIND_DIRECTION_DEG) \
	X(SceneRenderer, WIND_STRENGTH) X(SceneRenderer, WIND_WAVELENGTH_MM) X(SceneRenderer, WIND_SPEED) X(SceneRenderer, GUST_STRENGTH) \
	X(SceneRenderer, LEAN_SPREAD_DEG) X(SceneRenderer, WATER_EDGE_MM) X(SceneRenderer, WATER_DEEP_MM) X(SceneRenderer, RIPPLE_NORMAL_GAIN) \
	X(SceneRenderer, SPEC_STRENGTH) X(SceneRenderer, SHININESS) X(SceneRenderer, RIPPLE_SHIMMER) X(SceneRenderer, HIGHLIGHT_MAX) X(SceneRenderer, REFLECT_STRENGTH) \
	X(SceneRenderer, REFLECT_DIRECTION_DEG) X(SceneRenderer, REFLECT_DISTANCE_MM) X(SceneRenderer, REFLECT_AMP_MM) \
	X(SceneRenderer, REFLECT_FREQ) X(SceneRenderer, REFLECT_SPEED) X(SceneRenderer, BLACK_POINT) X(SceneRenderer, BLACK_SOFTNESS) \
	X(SceneHeightInput, MOCK_WATER_LEVEL_MM) X(SceneHeightInput, MOCK_HAND_SPEED) X(SceneHeightInput, MOCK_NOISE_MM) \
	X(SceneHeightInput, SMOOTH_RADIUS_CELLS) X(SceneHeightInput, SMOOTH_TIME_SEC) X(SceneHeightInput, WATER_THRESHOLD_MM) \
	X(RippleField, WAVE_C2) X(RippleField, DAMPING_PER_SEC) X(RippleField, MAX_AMPLITUDE) X(RippleField, HEIGHT_RATE_THRESHOLD) \
	X(RippleField, WATER_RATE_THRESHOLD) X(RippleField, HEIGHT_GAIN) X(RippleField, WATER_GAIN) X(RippleField, IMPULSE_CAP) \
	X(RippleField, DROP_RATE) X(RippleField, DROP_AMP) \
	X(SceneStylizer, CELL_MM) X(SceneStylizer, LUMINANCE_GAMMA) X(SceneStylizer, COLOUR_BOOST) X(SceneStylizer, BOTH_THRESHOLD)
#define SCENE_INTS(X) \
	X(SceneRenderer, SCENE_MULT) X(RippleField, RIPPLE_MULT) X(RippleField, SUBSTEPS) X(SceneStylizer, STYLE) \
	X(SceneStylizer, DOT_SIZE) X(SceneStylizer, LEVELS) X(SceneStylizer, PALETTE_PRESET) X(SceneStylizer, PALETTE_STOPS)
#define SCENE_BOOLS(X) \
	X(SceneRenderer, FREEZE) X(SceneHeightInput, MOCK_TERRAIN) X(SceneHeightInput, MOCK_FLAT) X(SceneHeightInput, MOCK_HAND) X(SceneStylizer, BAYER8) \
	X(SceneStylizer, PALETTE_ON) X(RippleField, SOURCE_HEIGHT) X(RippleField, SOURCE_WATER)
#define SCENE_COLOURS(X) \
	X(SceneRenderer, TERRAIN_LOW) X(SceneRenderer, TERRAIN_MID) X(SceneRenderer, TERRAIN_HIGH) X(SceneRenderer, SUN_COLOR) \
	X(SceneRenderer, GRASS_BASE) X(SceneRenderer, GRASS_TIP) X(SceneRenderer, WATER_SHALLOW) X(SceneRenderer, WATER_DEEP) \
	X(SceneStylizer, PALETTE[0]) X(SceneStylizer, PALETTE[1]) X(SceneStylizer, PALETTE[2])

static std::string colourString(const ofFloatColor & c)
{
	return ofToString(c.r) + "," + ofToString(c.g) + "," + ofToString(c.b);
}

static std::string tagName(const std::string & cls, const std::string & name)
{
	std::string t = cls + "_" + name;
	for (char & ch : t)
		if (ch == '[' || ch == ']')
			ch = '_';
	return t;
}

bool SceneRenderer::savePreset()
{
	ofXml xml;
	auto s = xml.appendChild("SCENEPRESET");
	s.appendChild("name").set(std::string(PRESET_NAME));
#define SAVE_V(C, N) s.appendChild(tagName(#C, #N)).set(C::N);
#define SAVE_B(C, N) s.appendChild(tagName(#C, #N)).set(C::N ? 1 : 0);
#define SAVE_C(C, N) s.appendChild(tagName(#C, #N)).set(colourString(C::N));
	SCENE_FLOATS(SAVE_V)
	SCENE_INTS(SAVE_V)
	SCENE_BOOLS(SAVE_B)
	SCENE_COLOURS(SAVE_C)
#undef SAVE_V
#undef SAVE_B
#undef SAVE_C
	std::string codes;
	for (size_t i = 0; i < SceneStylizer::RAMP.size(); i++)
		codes += (i ? "," : "") + ofToString((int)(unsigned char)SceneStylizer::RAMP[i]);
	s.appendChild("RAMP_CODES").set(codes);
	const bool ok = xml.save(PRESET_FILE);
	presetStatus = ok ? std::string("saved \"") + PRESET_NAME + "\"" : "save failed";
	return ok;
}

// Reads only the entries present, so an older file keeps newer defaults.
bool SceneRenderer::loadPreset()
{
	ofXml xml;
	if (!xml.load(PRESET_FILE)) { presetStatus = "no preset file"; return false; }
	auto s = xml.getChild("SCENEPRESET");
	if (!s) { presetStatus = "preset file not recognised"; return false; }
#define LOAD_F(C, N) if (s.getChild(tagName(#C, #N))) C::N = s.getChild(tagName(#C, #N)).getFloatValue();
#define LOAD_I(C, N) if (s.getChild(tagName(#C, #N))) C::N = s.getChild(tagName(#C, #N)).getIntValue();
#define LOAD_B(C, N) if (s.getChild(tagName(#C, #N))) C::N = s.getChild(tagName(#C, #N)).getIntValue() != 0;
#define LOAD_C(C, N) if (s.getChild(tagName(#C, #N))) { \
		const std::vector<std::string> v = ofSplitString(s.getChild(tagName(#C, #N)).getValue(), ",", true, true); \
		if (v.size() == 3) C::N = ofFloatColor(ofToFloat(v[0]), ofToFloat(v[1]), ofToFloat(v[2])); }
	SCENE_FLOATS(LOAD_F)
	SCENE_INTS(LOAD_I)
	SCENE_BOOLS(LOAD_B)
	SCENE_COLOURS(LOAD_C)
#undef LOAD_F
#undef LOAD_I
#undef LOAD_B
#undef LOAD_C
	if (s.getChild("RAMP_CODES")) {
		std::string r;
		for (const std::string & c : ofSplitString(s.getChild("RAMP_CODES").getValue(), ",", true, true)) {
			const int code = ofToInt(c);
			if (code >= 32 && code <= 126)
				r.push_back((char)code);
		}
		if (r.size() >= 2)
			SceneStylizer::RAMP = r;
	}
	const size_t n = std::min(SceneStylizer::RAMP.size(), sizeof(rampBuf) - 1);
	std::memcpy(rampBuf, SceneStylizer::RAMP.data(), n);
	rampBuf[n] = '\0';
	presetStatus = std::string("loaded \"") + s.getChild("name").getValue() + "\"";
	return true;
}

// ------------------------------------------------------------ GUI
void SceneRenderer::drawGui()
{
	typedef SceneRenderer R;
	typedef SceneStylizer S;
	typedef SceneHeightInput I;
	typedef RippleField W;
	ImGui::Begin("Scene");
	ImGui::Text("Prototype 4: a lit, animated scene through a stylise filter.");
	ImGui::Text("Display only: reads elevation and the simulated water, never writes them.");
	if (glFailed)
		ImGui::Text("Shaders or font not loaded (bin/data/shaders/scene, bin/data/ascii/fonts).");
	if (ImGui::Button("Save preset"))
		savePreset();
	ImGui::SameLine();
	if (ImGui::Button("Load preset"))
		loadPreset();
	if (!presetStatus.empty())
		ImGui::Text("Preset: %s", presetStatus.c_str());
	ImGui::Checkbox("Freeze animation (no wind, dapple drift or drops)", &R::FREEZE);
	ImGui::Text("GPU ms: input %.2f, ripples %.2f, scene %.2f, warp %.2f, style %.2f; CPU ms: input %.2f, mesh %.2f",
		tInput.ms(), tRipple.ms(), tScene.ms(), tWarp.ms(), tStyle.ms(), cpuInputMs, cpuMeshMs);
	ImGui::Text("fps %.1f; scene %d x %d, ripples x%d", ofGetFrameRate(), sceneCols, sceneRows, ripples.getMult());

	if (ImGui::CollapsingHeader("Style", ImGuiTreeNodeFlags_DefaultOpen)) {
		static const char * styles[] = { "Off (plain scene)", "ASCII", "Dither", "Both" };
		ImGui::Combo("Style##filter", &S::STYLE, styles, 4); // own ID: the section header is also "Style"
		ImGui::SliderFloat("cellSize (mm)", &S::CELL_MM, 2.0f, 30.0f);
		if (scaleMeasured)
			ImGui::Text("%.2f projector px/mm: cell %d x %d px (font %d px)", pxPerMM, stylizer.getCellWidth(),
				stylizer.getCellHeight(), stylizer.getFontSize());
		if (ImGui::Button("Re-measure scale"))
			scaleMeasured = false;
		ImGui::SliderFloat("luminanceGamma", &S::LUMINANCE_GAMMA, 0.2f, 3.0f);
		ImGui::SliderFloat("Black point", &R::BLACK_POINT, 0.0f, 0.5f);
		ImGui::SliderFloat("Black softness", &R::BLACK_SOFTNESS, 0.0f, 0.2f);
		ImGui::SliderFloat("Colour boost", &S::COLOUR_BOOST, 0.5f, 4.0f);
		if (ImGui::InputText("Glyph ramp", rampBuf, sizeof(rampBuf))) {
			std::string r(rampBuf);
			if (r.size() >= 2)
				S::RAMP = r;
		}
		ImGui::Text("Sorted by ink: \"%s\"", stylizer.getSortedRamp().c_str());
		ImGui::SliderInt("Dot size (px)", &S::DOT_SIZE, 1, 8);
		ImGui::SliderInt("Levels per channel", &S::LEVELS, 2, 8);
		ImGui::Checkbox("Bayer 8 x 8 (off: 4 x 4)", &S::BAYER8);
		ImGui::Checkbox("Palette", &S::PALETTE_ON);
		static const char * presets[] = { "Gold", "Night", "Green" };
		if (ImGui::Combo("Palette preset", &S::PALETTE_PRESET, presets, 3))
			SceneStylizer::applyPalettePreset(S::PALETTE_PRESET);
		ImGui::SliderInt("Palette stops", &S::PALETTE_STOPS, 2, 3);
		ImGui::ColorEdit3("Palette dark", &S::PALETTE[0].r);
		ImGui::ColorEdit3("Palette mid", &S::PALETTE[1].r);
		ImGui::ColorEdit3("Palette light", &S::PALETTE[2].r);
		ImGui::SliderFloat("Both: glyphs above luminance", &S::BOTH_THRESHOLD, 0.0f, 1.0f);
	}
	if (ImGui::CollapsingHeader("Light and terrain", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderFloat("Sun direction (deg)", &R::SUN_AZIMUTH_DEG, 0.0f, 360.0f);
		ImGui::SliderFloat("Sun elevation (deg)", &R::SUN_ELEVATION_DEG, 5.0f, 90.0f);
		ImGui::ColorEdit3("Sun colour", &R::SUN_COLOR.r);
		ImGui::SliderFloat("Ambient", &R::AMBIENT, 0.0f, 1.0f);
		ImGui::SliderFloat("Relief exaggeration", &R::EXAGGERATION, 0.5f, 10.0f);
		ImGui::SliderFloat("dappledLightSpeed (mm/s)", &R::DAPPLED_LIGHT_SPEED, 0.0f, 40.0f);
		ImGui::SliderFloat("Dapple direction (deg)", &R::DAPPLE_DIRECTION_DEG, 0.0f, 360.0f);
		ImGui::SliderFloat("Dapple scale (mm)", &R::DAPPLE_SCALE_MM, 20.0f, 400.0f);
		ImGui::SliderFloat("Dapple contrast", &R::DAPPLE_CONTRAST, 0.05f, 1.0f);
		ImGui::SliderFloat("Shade level", &R::DAPPLE_SHADE, 0.0f, 1.0f);
		ImGui::SliderFloat("Terrain low (mm)", &R::HEIGHT_LO_MM, -100.0f, 50.0f);
		ImGui::SliderFloat("Terrain high (mm)", &R::HEIGHT_HI_MM, 0.0f, 150.0f);
		ImGui::SliderFloat("Terrain mid stop", &R::TERRAIN_MID_POS, 0.05f, 0.95f);
		ImGui::ColorEdit3("Terrain low colour", &R::TERRAIN_LOW.r);
		ImGui::ColorEdit3("Terrain mid colour", &R::TERRAIN_MID.r);
		ImGui::ColorEdit3("Terrain high colour", &R::TERRAIN_HIGH.r);
	}
	if (ImGui::CollapsingHeader("Grass and wind", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderFloat("windStrength", &R::WIND_STRENGTH, 0.0f, 1.5f);
		ImGui::SliderFloat("Wind direction (deg)", &R::WIND_DIRECTION_DEG, 0.0f, 360.0f);
		ImGui::SliderFloat("Wind wave length (mm)", &R::WIND_WAVELENGTH_MM, 20.0f, 600.0f);
		ImGui::SliderFloat("Wind wave speed (/s)", &R::WIND_SPEED, 0.0f, 2.0f);
		ImGui::SliderFloat("Gusts", &R::GUST_STRENGTH, 0.0f, 2.0f);
		ImGui::SliderFloat("Blade length (mm)", &R::BLADE_LENGTH_MM, 1.0f, 15.0f);
		ImGui::SliderFloat("Blade width (mm)", &R::BLADE_WIDTH_MM, 0.2f, 3.0f);
		ImGui::SliderFloat("Blades per tuft", &R::BLADES_PER_TUFT, 0.0f, 6.0f);
		ImGui::SliderFloat("Lean spread (deg)", &R::LEAN_SPREAD_DEG, 0.0f, 90.0f);
		ImGui::SliderFloat("Grass from (mm)", &R::GRASS_MIN_MM, -60.0f, 60.0f);
		ImGui::SliderFloat("Grass to (mm)", &R::GRASS_MAX_MM, -20.0f, 120.0f);
		ImGui::SliderFloat("Grass max slope", &R::GRASS_MAX_SLOPE, 0.05f, 3.0f);
		ImGui::SliderFloat("Clump scale (mm)", &R::CLUMP_SCALE_MM, 10.0f, 300.0f);
		ImGui::SliderFloat("Clumping", &R::CLUMP_AMOUNT, 0.0f, 1.0f);
		ImGui::ColorEdit3("Grass base", &R::GRASS_BASE.r);
		ImGui::ColorEdit3("Grass tip", &R::GRASS_TIP.r);
		ImGui::SliderFloat("Tip brightening", &R::TIP_BOOST, 1.0f, 3.0f);
	}
	if (ImGui::CollapsingHeader("Water and ripples", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderFloat("Water threshold (mm)", &I::WATER_THRESHOLD_MM, 0.1f, 10.0f);
		ImGui::SliderFloat("Shoreline softness (mm)", &R::WATER_EDGE_MM, 0.1f, 10.0f);
		ImGui::SliderFloat("Deep at (mm)", &R::WATER_DEEP_MM, 2.0f, 80.0f);
		ImGui::ColorEdit3("Water shallow", &R::WATER_SHALLOW.r);
		ImGui::ColorEdit3("Water deep", &R::WATER_DEEP.r);
		ImGui::SliderFloat("Specular", &R::SPEC_STRENGTH, 0.0f, 4.0f);
		ImGui::SliderFloat("Shininess", &R::SHININESS, 2.0f, 400.0f);
		ImGui::SliderFloat("Ripple shimmer", &R::RIPPLE_SHIMMER, 0.0f, 3.0f);
		ImGui::SliderFloat("Highlight max", &R::HIGHLIGHT_MAX, 0.05f, 1.0f);
		ImGui::SliderFloat("Ripple normal gain", &R::RIPPLE_NORMAL_GAIN, 0.0f, 20.0f);
		ImGui::SliderFloat("Reflection strength", &R::REFLECT_STRENGTH, 0.0f, 1.0f);
		ImGui::SliderFloat("Reflection direction (deg)", &R::REFLECT_DIRECTION_DEG, 0.0f, 360.0f);
		ImGui::SliderFloat("Reflection distance (mm)", &R::REFLECT_DISTANCE_MM, 0.0f, 100.0f);
		ImGui::SliderFloat("Reflection wobble (mm)", &R::REFLECT_AMP_MM, 0.0f, 10.0f);
		ImGui::SliderFloat("Wobble frequency (/mm)", &R::REFLECT_FREQ, 0.01f, 1.0f);
		ImGui::SliderFloat("Wobble speed (rad/s)", &R::REFLECT_SPEED, 0.0f, 6.0f);
		ImGui::SliderFloat("Ripple damping (/s)", &W::DAMPING_PER_SEC, 0.0f, 10.0f);
		ImGui::SliderFloat("Ripple max height", &W::MAX_AMPLITUDE, 0.05f, 3.0f);
		ImGui::SliderFloat("Ripple speed (c^2)", &W::WAVE_C2, 0.02f, 0.5f);
		ImGui::SliderInt("Ripple substeps (at 60 fps)", &W::SUBSTEPS, 1, 8);
		ImGui::SliderInt("Ripple resolution (x grid)", &W::RIPPLE_MULT, 1, 4);
		ImGui::Checkbox("Rings from sand changes", &W::SOURCE_HEIGHT);
		ImGui::SameLine();
		ImGui::Checkbox("Rings from water changes", &W::SOURCE_WATER);
		ImGui::SliderFloat("Threshold, sand (mm/s)", &W::HEIGHT_RATE_THRESHOLD, 0.0f, 100.0f);
		ImGui::SliderFloat("Threshold, water (mm/s)", &W::WATER_RATE_THRESHOLD, 0.0f, 100.0f);
		ImGui::SliderFloat("Gain, sand (/s per mm/s)", &W::HEIGHT_GAIN, 0.0f, 0.5f);
		ImGui::SliderFloat("Gain, water (/s per mm/s)", &W::WATER_GAIN, 0.0f, 0.5f);
		ImGui::SliderFloat("Impulse cap (/s)", &W::IMPULSE_CAP, 0.0f, 10.0f);
		ImGui::SliderFloat("Drops (/s per 0.1 m^2)", &W::DROP_RATE, 0.0f, 10.0f);
		ImGui::SliderFloat("Drop size", &W::DROP_AMP, 0.0f, 4.0f);
	}
	if (ImGui::CollapsingHeader("Input", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderFloat("Height smoothing radius (cells)", &I::SMOOTH_RADIUS_CELLS, 0.0f, 4.0f);
		ImGui::SliderFloat("Height smoothing time (s)", &I::SMOOTH_TIME_SEC, 0.0f, 3.0f);
		ImGui::SliderInt("Scene resolution (x grid)", &R::SCENE_MULT, 1, 6);
		ImGui::Checkbox("Mock terrain", &I::MOCK_TERRAIN);
		ImGui::Checkbox("Mock flat (no mound, ridge or basin)", &I::MOCK_FLAT);
		ImGui::SliderFloat("Mock water level (mm)", &I::MOCK_WATER_LEVEL_MM, -40.0f, 20.0f);
		ImGui::Checkbox("Mock hand", &I::MOCK_HAND);
		ImGui::SliderFloat("Mock hand speed (turns/min)", &I::MOCK_HAND_SPEED, 0.0f, 10.0f);
		ImGui::SliderFloat("Mock noise (+- mm per frame)", &I::MOCK_NOISE_MM, 0.0f, 3.0f);
	}
	ImGui::End();
}
