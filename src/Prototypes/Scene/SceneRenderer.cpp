#include "SceneRenderer.h"
#include "../../KinectProjector/KinectProjector.h"
#include "../../Games/VegetationField.h"
#include "ofxImGui.h"
#include <cstring>
#include <cfloat>

static ofFloatColor hexColor(unsigned int h)
{
	return ofFloatColor(((h >> 16) & 0xff) / 255.0f, ((h >> 8) & 0xff) / 255.0f, (h & 0xff) / 255.0f);
}

// Defaults (DEVLOG, 2026-11-19, step 1b: mostly black with bright
// features; chosen on the headless preview, not yet on the sand).
int SceneRenderer::SCENE_MULT = 2;     // blades are now several cells wide: 2 x grid is enough
bool SceneRenderer::FREEZE = false;
int SceneRenderer::RANGE_MODE = SceneRenderer::RANGE_CALIBRATED;
float SceneRenderer::RANGE_MIN_MM = -142.6f;
float SceneRenderer::RANGE_MAX_MM = 142.6f;
// Fractions of the range (this box: +-142.6 mm; flat sand 9 mm = 0.53).
float SceneRenderer::TERRAIN_LO_FRAC = 0.40f;
float SceneRenderer::TERRAIN_HI_FRAC = 0.78f;
float SceneRenderer::TERRAIN_MID_POS = 0.45f;
ofFloatColor SceneRenderer::TERRAIN_LOW = hexColor(0x020806);   // ground near black
ofFloatColor SceneRenderer::TERRAIN_MID = hexColor(0x0A1A0E);
ofFloatColor SceneRenderer::TERRAIN_HIGH = hexColor(0x5A4630);  // high ground: dark warm sand
float SceneRenderer::EXAGGERATION = 1.5f;
float SceneRenderer::SUN_AZIMUTH_DEG = 225.0f;                 // from the upper left (grid y down)
float SceneRenderer::SUN_ELEVATION_DEG = 40.0f;
ofFloatColor SceneRenderer::SUN_COLOR = hexColor(0xFFE9C4);
float SceneRenderer::AMBIENT = 0.12f;
float SceneRenderer::DAPPLE_SCALE_MM = 110.0f;
float SceneRenderer::DAPPLED_LIGHT_SPEED = 6.0f;               // mm/s
float SceneRenderer::DAPPLE_DIRECTION_DEG = 30.0f;
float SceneRenderer::DAPPLE_CONTRAST = 0.85f;
float SceneRenderer::DAPPLE_SHADE = 0.08f;
float SceneRenderer::BLADE_WIDTH_CELLS = 0.8f;
float SceneRenderer::BLADE_LENGTH_CELLS = 4.0f;
float SceneRenderer::TUFT_SPACING_CELLS = 1.5f;
float SceneRenderer::BLADE_WIDTH_DOTS = 2.5f;
float SceneRenderer::BLADE_LENGTH_DOTS = 16.0f;
float SceneRenderer::TUFT_SPACING_DOTS = 6.5f;
float SceneRenderer::BLADES_PER_TUFT = 3.0f;
float SceneRenderer::GRASS_MIN_FRAC = 0.48f;   // -6 mm at +-142.6
float SceneRenderer::GRASS_MAX_FRAC = 0.66f;   // 45 mm
float SceneRenderer::GRASS_MAX_SLOPE = 0.9f;
float SceneRenderer::CLUMP_SCALE_MM = 60.0f;
float SceneRenderer::CLUMP_AMOUNT = 0.6f;
ofFloatColor SceneRenderer::GRASS_BASE = hexColor(0x0E3A18);
ofFloatColor SceneRenderer::GRASS_TIP = hexColor(0x9BE86A);
float SceneRenderer::TIP_BOOST = 2.0f;
float SceneRenderer::WIND_DIRECTION_DEG = 80.0f;
float SceneRenderer::WIND_STRENGTH = 0.35f;
float SceneRenderer::WIND_WAVELENGTH_MM = 150.0f;
float SceneRenderer::WIND_SPEED = 0.25f;                       // waves per second
float SceneRenderer::GUST_STRENGTH = 0.8f;
float SceneRenderer::LEAN_SPREAD_DEG = 35.0f;
float SceneRenderer::WATER_EDGE_MM = 2.0f;
float SceneRenderer::WATER_DEEP_MM = 30.0f;
ofFloatColor SceneRenderer::WATER_SHALLOW = hexColor(0x041416);
ofFloatColor SceneRenderer::WATER_DEEP = hexColor(0x010304);
float SceneRenderer::RIPPLE_NORMAL_GAIN = 12.0f;
float SceneRenderer::SPEC_STRENGTH = 1.2f;
float SceneRenderer::SHININESS = 60.0f;
float SceneRenderer::RIPPLE_SHIMMER = 1.2f;
float SceneRenderer::HIGHLIGHT_MAX = 0.7f;  // water glints never above this (no white-out)
float SceneRenderer::REFLECT_STRENGTH = 0.25f;
float SceneRenderer::REFLECT_DIRECTION_DEG = 90.0f;
float SceneRenderer::REFLECT_DISTANCE_MM = 25.0f;
float SceneRenderer::REFLECT_AMP_MM = 2.0f;
float SceneRenderer::REFLECT_FREQ = 0.15f;
float SceneRenderer::REFLECT_SPEED = 1.5f;
float SceneRenderer::BLACK_POINT = 0.04f;
float SceneRenderer::BLACK_SOFTNESS = 0.03f;
float SceneRenderer::EXPOSURE = 1.0f;
float SceneRenderer::TOE = 1.5f;
float SceneRenderer::SHOULDER = 0.6f;
bool SceneRenderer::AUTO_EXPOSURE = false;
float SceneRenderer::AUTO_TARGET = 0.85f;
float SceneRenderer::AUTO_TIME_SEC = 3.0f;

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
			&& SceneGL::load(probeShader, "sceneProbe")
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
	updateRange();
	updateGrassMM();

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
	probeScene(dt);
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
	s.setUniform1f("rangeLo", rangeLoMM);
	s.setUniform1f("rangeHi", rangeHiMM);
	s.setUniform1f("terrainLoFrac", TERRAIN_LO_FRAC);
	s.setUniform1f("terrainHiFrac", std::max(TERRAIN_LO_FRAC + 0.01f, TERRAIN_HI_FRAC));
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
	s.setUniform1f("bladeLength", bladeLenMM);
	s.setUniform1f("bladeWidth", bladeWidthMM);
	s.setUniform1f("tuftSpacing", tuftMM);
	s.setUniform1f("bladesPerTuft", ofClamp(BLADES_PER_TUFT, 0.0f, 6.0f));
	s.setUniform1f("grassMinFrac", GRASS_MIN_FRAC);
	s.setUniform1f("grassMaxFrac", GRASS_MAX_FRAC);
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
	s.setUniform1f("exposure", std::max(0.0f, EXPOSURE) * (AUTO_EXPOSURE ? autoGain : 1.0f));
	s.setUniform1f("toe", std::max(1.0f, TOE));
	s.setUniform1f("shoulder", std::max(0.0f, SHOULDER));
	s.setUniform1f("blackPoint", BLACK_POINT);
	s.setUniform1f("blackSoftness", std::max(0.0f, BLACK_SOFTNESS));
	sceneQuad.draw();
	s.end();
	sceneFbo.end();
	ofPopStyle();
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
}

// The active elevation range, sorted (as Prototype 3's AsciiRenderer).
void SceneRenderer::updateRange()
{
	float a = 0.0f, b = 0.0f;
	if (RANGE_MODE == RANGE_VEGETATION && vegetation) {
		vegetation->getElevationRange(a, b);
	} else if (RANGE_MODE == RANGE_CUSTOM) {
		a = RANGE_MIN_MM;
		b = RANGE_MAX_MM;
	} else {
		const float c = kinectProjector ? std::fabs(kinectProjector->getCalibratedCeilingElevation()) : 0.0f;
		a = -c;
		b = c;
	}
	rangeLoMM = std::min(a, b);
	rangeHiMM = std::max(a, b);
	if (!(rangeHiMM - rangeLoMM >= 1.0f)) {
		const float m = std::isfinite(a + b) ? 0.5f * (a + b) : 0.0f;
		rangeLoMM = m - 0.5f;
		rangeHiMM = m + 0.5f;
	}
}

// Grass sizes in mm from the stylise units: glyph cell width (blade width)
// and height (blade length, tuft spacing), or the dither dot, at the
// measured projector scale. Recomputed every frame, so they follow
// cellSize, dot size and the calibration.
void SceneRenderer::updateGrassMM()
{
	const float ppm = std::max(1e-3f, pxPerMM);
	if (SceneStylizer::STYLE == SceneStylizer::STYLE_DITHER) {
		const float dot = std::max(1, SceneStylizer::DOT_SIZE) / ppm;
		bladeWidthMM = BLADE_WIDTH_DOTS * dot;
		bladeLenMM = BLADE_LENGTH_DOTS * dot;
		tuftMM = TUFT_SPACING_DOTS * dot;
	} else {
		const float cw = std::max(1, stylizer.getCellWidth()) / ppm, ch = std::max(1, stylizer.getCellHeight()) / ppm;
		bladeWidthMM = BLADE_WIDTH_CELLS * cw;
		bladeLenMM = BLADE_LENGTH_CELLS * ch;
		tuftMM = TUFT_SPACING_CELLS * ch;
	}
	bladeWidthMM = std::max(0.2f, bladeWidthMM);
	bladeLenMM = std::max(0.5f, bladeLenMM);
	tuftMM = std::max(0.5f, tuftMM);
}

// The scene's final luminance on a 160 x 90 probe, read back every 10
// frames: the GUI histogram (32 bins), its 95th percentile, and the
// optional auto-exposure (gain eased towards AUTO_TARGET / p95, in the log
// domain, time constant AUTO_TIME_SEC).
void SceneRenderer::probeScene(float dt)
{
	const int pw = 160, ph = 90;
	if (!probeFbo.isAllocated()) {
		SceneGL::allocate(probeFbo, pw, ph, GL_RGBA32F, GL_NEAREST);
		probeQuad = SceneGL::quad((float)pw, (float)ph);
	}
	if (--probeCountdown > 0) {
		if (AUTO_EXPOSURE && p95Ema > 0.0f)
			autoGain = ofClamp(autoGain * std::pow(AUTO_TARGET / std::max(1e-3f, p95Ema), dt / std::max(0.1f, AUTO_TIME_SEC)), 0.25f, 8.0f);
		return;
	}
	probeCountdown = 10;
	ofPushStyle();
	ofEnableBlendMode(OF_BLENDMODE_DISABLED);
	probeFbo.begin();
	ofClear(0, 0, 0, 0);
	probeShader.begin();
	probeShader.setUniformTexture("sceneTex", sceneFbo.getTexture(), 0);
	probeShader.setUniform2f("scale", (float)sceneCols / pw, (float)sceneRows / ph);
	probeQuad.draw();
	probeShader.end();
	probeFbo.end();
	ofPopStyle();
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
	probeFbo.readToPixels(probePix);
	std::vector<float> lum;
	lum.reserve(pw * ph);
	for (int i = 0; i < 32; i++)
		histogram[i] = 0.0f;
	const float * p = probePix.getData();
	const size_t nc = probePix.getNumChannels();
	for (size_t i = 0; i < (size_t)pw * ph; i++) {
		const float l = ofClamp(p[i * nc], 0.0f, 1.0f);
		lum.push_back(l);
		histogram[std::min(31, (int)(l * 32.0f))] += 1.0f;
	}
	std::nth_element(lum.begin(), lum.begin() + lum.size() * 95 / 100, lum.end());
	p95 = lum[lum.size() * 95 / 100];
	const float a = 1.0f - std::exp(-10.0f * std::max(1e-3f, dt) / std::max(0.1f, AUTO_TIME_SEC));
	p95Ema = p95Ema < 0.0f ? p95 : p95Ema + (p95 - p95Ema) * a;
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
	X(SceneRenderer, RANGE_MIN_MM) X(SceneRenderer, RANGE_MAX_MM) X(SceneRenderer, TERRAIN_LO_FRAC) X(SceneRenderer, TERRAIN_HI_FRAC) \
	X(SceneRenderer, TERRAIN_MID_POS) X(SceneRenderer, EXAGGERATION) \
	X(SceneRenderer, BLADE_WIDTH_CELLS) X(SceneRenderer, BLADE_LENGTH_CELLS) X(SceneRenderer, TUFT_SPACING_CELLS) \
	X(SceneRenderer, BLADE_WIDTH_DOTS) X(SceneRenderer, BLADE_LENGTH_DOTS) X(SceneRenderer, TUFT_SPACING_DOTS) \
	X(SceneRenderer, GRASS_MIN_FRAC) X(SceneRenderer, GRASS_MAX_FRAC) X(SceneRenderer, EXPOSURE) X(SceneRenderer, TOE) \
	X(SceneRenderer, SHOULDER) X(SceneRenderer, AUTO_TARGET) X(SceneRenderer, AUTO_TIME_SEC) X(SceneStylizer, EDGE_THRESHOLD) \
	X(SceneStylizer, WEIGHT_SPLIT) X(RippleField, DROP_RADIUS_MM) \
	X(SceneRenderer, SUN_AZIMUTH_DEG) X(SceneRenderer, SUN_ELEVATION_DEG) X(SceneRenderer, AMBIENT) \
	X(SceneRenderer, DAPPLE_SCALE_MM) X(SceneRenderer, DAPPLED_LIGHT_SPEED) X(SceneRenderer, DAPPLE_DIRECTION_DEG) \
	X(SceneRenderer, DAPPLE_CONTRAST) X(SceneRenderer, DAPPLE_SHADE) \
	X(SceneRenderer, BLADES_PER_TUFT) X(SceneRenderer, GRASS_MAX_SLOPE) \
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
	X(SceneRenderer, SCENE_MULT) X(SceneRenderer, RANGE_MODE) X(SceneStylizer, LINE_PERIOD) X(RippleField, RIPPLE_MULT) X(RippleField, SUBSTEPS) X(SceneStylizer, STYLE) \
	X(SceneStylizer, DOT_SIZE) X(SceneStylizer, LEVELS) X(SceneStylizer, PALETTE_PRESET) X(SceneStylizer, PALETTE_STOPS)
#define SCENE_BOOLS(X) \
	X(SceneRenderer, FREEZE) X(SceneHeightInput, MOCK_TERRAIN) X(SceneHeightInput, MOCK_FLAT) X(SceneHeightInput, MOCK_HAND) X(SceneStylizer, BAYER8) \
	X(SceneStylizer, PALETTE_ON) X(SceneStylizer, LINE_SCREEN) X(SceneStylizer, DEBUG_EDGES) X(SceneRenderer, AUTO_EXPOSURE) X(RippleField, SOURCE_HEIGHT) X(RippleField, SOURCE_WATER)
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
	std::string dirCodes;
	for (size_t i = 0; i < SceneStylizer::DIR_GLYPHS.size(); i++)
		dirCodes += (i ? "," : "") + ofToString((int)(unsigned char)SceneStylizer::DIR_GLYPHS[i]);
	s.appendChild("DIR_CODES").set(dirCodes);
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
	if (s.getChild("DIR_CODES")) {
		std::string r;
		for (const std::string & c : ofSplitString(s.getChild("DIR_CODES").getValue(), ",", true, true)) {
			const int code = ofToInt(c);
			if (code >= 32 && code <= 126)
				r.push_back((char)code);
		}
		if (r.size() == 8)
			SceneStylizer::DIR_GLYPHS = r;
	}
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
		ImGui::SliderFloat("Edge threshold", &S::EDGE_THRESHOLD, 0.0f, 0.3f);
		{
			static char dirBuf[16];
			const size_t n = std::min(S::DIR_GLYPHS.size(), sizeof(dirBuf) - 1);
			std::memcpy(dirBuf, S::DIR_GLYPHS.data(), n);
			dirBuf[n] = '\0';
			if (ImGui::InputText("Directional glyphs (light/heavy: - \\ | /)", dirBuf, 9)) {
				std::string r(dirBuf);
				if (r.size() == 8)
					S::DIR_GLYPHS = r;
			}
		}
		ImGui::SliderFloat("Heavy glyph above", &S::WEIGHT_SPLIT, 0.0f, 1.0f);
		ImGui::Checkbox("Debug: edge cells by direction", &S::DEBUG_EDGES);
		ImGui::Text("(cyan -, amber \\, magenta |, white /; grey: no edge)");
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
		ImGui::Checkbox("Line screen (horizontal 1-px lines)", &S::LINE_SCREEN);
		ImGui::SliderInt("Line period (px)", &S::LINE_PERIOD, 2, 8);
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
	if (ImGui::CollapsingHeader("Exposure", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderFloat("Exposure", &R::EXPOSURE, 0.1f, 8.0f);
		ImGui::SliderFloat("Toe (shadows)", &R::TOE, 1.0f, 3.0f);
		ImGui::SliderFloat("Shoulder (highlights)", &R::SHOULDER, 0.0f, 4.0f);
		ImGui::Checkbox("Auto-exposure", &R::AUTO_EXPOSURE);
		ImGui::SameLine();
		ImGui::Text("gain x%.2f", AUTO_EXPOSURE ? autoGain : 1.0f);
		ImGui::SliderFloat("Auto target (95th pct)", &R::AUTO_TARGET, 0.2f, 1.0f);
		ImGui::SliderFloat("Auto time (s)", &R::AUTO_TIME_SEC, 0.5f, 20.0f);
		ImGui::PlotHistogram("Scene luminance", histogram, 32, 0, nullptr, 0.0f, FLT_MAX, ImVec2(0, 60));
		ImGui::Text("0 .. 1, after exposure and black point; 95th percentile %.2f", p95);
	}
	if (ImGui::CollapsingHeader("Elevation range", ImGuiTreeNodeFlags_DefaultOpen)) {
		static const char * rangeModes[] = { "Calibrated ceiling", "Match the Vegetation panel", "Custom" };
		ImGui::Combo("Elevation range##scene", &R::RANGE_MODE, rangeModes, 3);
		if (RANGE_MODE == RANGE_CUSTOM) {
			ImGui::SliderFloat("Range min (mm)", &R::RANGE_MIN_MM, -300.0f, 300.0f);
			ImGui::SliderFloat("Range max (mm)", &R::RANGE_MAX_MM, -300.0f, 300.0f);
			if (ImGui::Button("Reset to +-calibrated ceiling") && kinectProjector) {
				const float c = std::fabs(kinectProjector->getCalibratedCeilingElevation());
				RANGE_MIN_MM = -c;
				RANGE_MAX_MM = c;
			}
		}
		updateRange();
		ImGui::Text("Active range %.1f .. %.1f mm; terrain and grass below are fractions of it", rangeLoMM, rangeHiMM);
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
		ImGui::SliderFloat("Terrain low stop (fraction)", &R::TERRAIN_LO_FRAC, 0.0f, 1.0f);
		ImGui::SliderFloat("Terrain high stop (fraction)", &R::TERRAIN_HI_FRAC, 0.0f, 1.0f);
		ImGui::Text("(= %.1f .. %.1f mm)", rangeLoMM + TERRAIN_LO_FRAC * (rangeHiMM - rangeLoMM),
			rangeLoMM + TERRAIN_HI_FRAC * (rangeHiMM - rangeLoMM));
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
		if (S::STYLE == SceneStylizer::STYLE_DITHER) {
			ImGui::Text("Grass sizes in dither dots (Style: Dither):");
			ImGui::SliderFloat("Blade width (dots)", &R::BLADE_WIDTH_DOTS, 0.5f, 8.0f);
			ImGui::SliderFloat("Blade length (dots)", &R::BLADE_LENGTH_DOTS, 2.0f, 40.0f);
			ImGui::SliderFloat("Tuft spacing (dots)", &R::TUFT_SPACING_DOTS, 1.0f, 30.0f);
		} else {
			ImGui::Text("Grass sizes in glyph cells (Style: ASCII, Both, Off):");
			ImGui::SliderFloat("Blade width (cells)", &R::BLADE_WIDTH_CELLS, 0.2f, 3.0f);
			ImGui::SliderFloat("Blade length (cells)", &R::BLADE_LENGTH_CELLS, 0.5f, 10.0f);
			ImGui::SliderFloat("Tuft spacing (cells)", &R::TUFT_SPACING_CELLS, 0.3f, 6.0f);
		}
		ImGui::Text("= blade %.1f x %.1f mm, tufts every %.1f mm", bladeLenMM, bladeWidthMM, tuftMM);
		ImGui::SliderFloat("Blades per tuft", &R::BLADES_PER_TUFT, 0.0f, 6.0f);
		ImGui::SliderFloat("Lean spread (deg)", &R::LEAN_SPREAD_DEG, 0.0f, 90.0f);
		ImGui::SliderFloat("Grass from (fraction)", &R::GRASS_MIN_FRAC, 0.0f, 1.0f);
		ImGui::SliderFloat("Grass to (fraction)", &R::GRASS_MAX_FRAC, 0.0f, 1.0f);
		ImGui::Text("(= %.1f .. %.1f mm)", rangeLoMM + GRASS_MIN_FRAC * (rangeHiMM - rangeLoMM),
			rangeLoMM + GRASS_MAX_FRAC * (rangeHiMM - rangeLoMM));
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
		ImGui::SliderFloat("Drop radius (mm)", &W::DROP_RADIUS_MM, 1.0f, 30.0f);
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
