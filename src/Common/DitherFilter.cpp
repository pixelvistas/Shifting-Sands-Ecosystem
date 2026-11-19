#include "DitherFilter.h"
#include "ofxImGui.h"

static ofFloatColor hexColor(unsigned int h)
{
	return ofFloatColor(((h >> 16) & 0xff) / 255.0f, ((h >> 8) & 0xff) / 255.0f, (h & 0xff) / 255.0f);
}

// Palette presets (SceneStylizer's, prototype/4-scene): dark, mid, light.
static const unsigned int PALETTES[3][3] = {
	{ 0x1A1206, 0x9C7A2E, 0xF2D58A },   // Gold
	{ 0x05070D, 0x2E4466, 0xC9D6E8 },   // Night
	{ 0x020806, 0x2F7A55, 0xCFE8D2 } }; // Green

// Defaults (DEVLOG, 2026-11-18; chosen on the headless Sandbox preview,
// tools/sandbox_dither_preview, to be confirmed on the sand).
static DitherFilter::Params makeDefaults()
{
	DitherFilter::Params p;
	p.enabled = false;
	p.mode = DitherFilter::MODE_HUE;
	p.pattern = DitherFilter::PATTERN_BAYER4;
	p.dotSize = 2;
	p.levels = 3;
	p.blackPoint = 0.02f;
	p.exposure = 1.0f;
	p.contrast = 1.0f;
	p.saturation = 1.0f;
	p.lineProtect = DitherFilter::PROTECT_BRIGHT_DARK;
	p.lineThreshold = 0.10f;
	p.linePeriod = 2;
	p.halftoneAngle = 45.0f;
	p.halftoneCell = 4;
	p.paletteStops = 3;
	DitherFilter::applyPalettePreset(p, DitherFilter::PALETTE_GREEN);
	return p;
}
const DitherFilter::Params DitherFilter::DEFAULTS = makeDefaults();

static const char * PRESET_ROOT = "DITHERFILTER";

void DitherFilter::applyPalettePreset(Params & p, int preset)
{
	p.palettePreset = std::max(0, std::min(2, preset));
	for (int i = 0; i < 3; i++)
		p.palette[i] = hexColor(PALETTES[p.palettePreset][i]);
}

// ------------------------------------------------------------ GL
bool DitherFilter::loadShaders()
{
#ifdef TARGET_OPENGLES
	loadState = -1;
	return false;
#else
	const std::string dir = ofIsGLProgrammableRenderer() ? "shaders/dither/GL3/" : "shaders/dither/GL2/";
	const bool ok = dotsShader.load(dir + "dither.vert", dir + "ditherDots.frag")
		&& outShader.load(dir + "dither.vert", dir + "ditherOut.frag");
	if (!ok)
		ofLogError("DitherFilter") << "could not load the shaders in " << dir;
	loadState = ok ? 1 : -1;
	return ok;
#endif
}

bool DitherFilter::isReady()
{
	if (loadState == 0)
		loadShaders();
	return loadState == 1;
}

static ofMesh pixelQuad(float w, float h)
{
	ofMesh q;
	q.setMode(OF_PRIMITIVE_TRIANGLE_STRIP);
	q.addVertex(glm::vec3(0, 0, 0)); q.addTexCoord(glm::vec2(0, 0));
	q.addVertex(glm::vec3(w, 0, 0)); q.addTexCoord(glm::vec2(w, 0));
	q.addVertex(glm::vec3(0, h, 0)); q.addTexCoord(glm::vec2(0, h));
	q.addVertex(glm::vec3(w, h, 0)); q.addTexCoord(glm::vec2(w, h));
	return q;
}

static void allocateRect(ofFbo & fbo, int w, int h, GLint internalFormat, int samples)
{
	ofFboSettings s;
	s.width = std::max(1, w);
	s.height = std::max(1, h);
	s.internalformat = internalFormat;
	s.textureTarget = GL_TEXTURE_RECTANGLE_ARB;
	s.minFilter = GL_NEAREST;
	s.maxFilter = GL_NEAREST;
	s.wrapModeHorizontal = GL_CLAMP_TO_EDGE;
	s.wrapModeVertical = GL_CLAMP_TO_EDGE;
	s.useDepth = false;
	s.numSamples = std::max(0, samples);
	fbo.allocate(s);
}

bool DitherFilter::begin(int w, int h)
{
	if (capturing || !isReady() || w <= 0 || h <= 0)
		return false;
	// Multisample the capture like the target we are drawing to (the
	// window's default framebuffer here), so meshes drawn into it are
	// rasterised as they would be there.
	GLint samples = 0;
	glGetIntegerv(GL_SAMPLES, &samples);
	if (!captureFbo.isAllocated() || w != captureW || h != captureH || samples != captureSamples) {
		allocateRect(captureFbo, w, h, GL_RGBA, samples > 1 ? samples : 0);
		captureW = w;
		captureH = h;
		captureSamples = samples;
	}
	timer.begin();
	captureFbo.begin();
	ofClear(0, 0, 0, 255); // as the window: opaque black
	capturing = true;
	return true;
}

void DitherFilter::end()
{
	if (!capturing)
		return;
	captureFbo.end();
	capturing = false;
}

void DitherFilter::drawCaptured(float x, float y)
{
	if (capturing || !captureFbo.isAllocated())
		return;
	draw(captureFbo.getTexture(), x, y); // getTexture() resolves the multisampled capture
	timer.end();
}

void DitherFilter::ensureDots(int w, int h, int ds)
{
	const int cols = (w + ds - 1) / ds, rows = (h + ds - 1) / ds;
	if (!dotFbo.isAllocated() || cols != dotW || rows != dotH) {
		allocateRect(dotFbo, cols, rows, GL_RGBA16F, 0);
		dotW = cols;
		dotH = rows;
		dotQuad = pixelQuad((float)cols, (float)rows);
	}
	if (w != srcW || h != srcH || srcQuad.getNumVertices() == 0) {
		srcW = w;
		srcH = h;
		srcQuad = pixelQuad((float)w, (float)h);
	}
	dotDs = ds;
}

void DitherFilter::draw(const ofTexture & src, float x, float y)
{
	const int w = (int)src.getWidth(), h = (int)src.getHeight();
	ofPushStyle();
	ofEnableBlendMode(OF_BLENDMODE_DISABLED);
	ofSetColor(255);
	if (!isReady() || src.getTextureData().textureTarget != GL_TEXTURE_RECTANGLE_ARB || w <= 0 || h <= 0) {
		src.draw(x, y);
		ofPopStyle();
		ofEnableBlendMode(OF_BLENDMODE_ALPHA);
		return;
	}
	const Params & p = params;
	const int ds = std::max(1, std::min(8, p.dotSize));
	ensureDots(w, h, ds);
	const float exposure = ofClamp(p.exposure, 0.0f, 8.0f);
	const float contrast = ofClamp(p.contrast, 0.0f, 4.0f);
	const float saturation = ofClamp(p.saturation, 0.0f, 4.0f);

	dotFbo.begin();
	ofClear(0, 0, 0, 0);
	dotsShader.begin();
	dotsShader.setUniformTexture("srcTex", src, 0);
	dotsShader.setUniform2f("srcSize", (float)w, (float)h);
	dotsShader.setUniform1f("dotSize", (float)ds);
	dotsShader.setUniform1f("exposure", exposure);
	dotsShader.setUniform1f("contrast", contrast);
	dotsShader.setUniform1f("saturation", saturation);
	dotQuad.draw();
	dotsShader.end();
	dotFbo.end();

	float pal[9];
	for (int i = 0; i < 3; i++) {
		pal[3 * i] = p.palette[i].r; pal[3 * i + 1] = p.palette[i].g; pal[3 * i + 2] = p.palette[i].b;
	}
	const float a = ofDegToRad(p.halftoneAngle);
	outShader.begin();
	outShader.setUniformTexture("srcTex", src, 0);
	outShader.setUniformTexture("dotTex", dotFbo.getTexture(), 1);
	outShader.setUniform2f("srcSize", (float)w, (float)h);
	outShader.setUniform1f("dotSize", (float)ds);
	outShader.setUniform1f("exposure", exposure);
	outShader.setUniform1f("contrast", contrast);
	outShader.setUniform1f("saturation", saturation);
	outShader.setUniform1i("mode", std::max(0, std::min(2, p.mode)));
	outShader.setUniform1i("pattern", std::max(0, std::min(3, p.pattern)));
	outShader.setUniform1f("levels", (float)std::max(2, std::min(16, p.levels)));
	outShader.setUniform1f("blackPoint", ofClamp(p.blackPoint, 0.0f, 1.0f));
	outShader.setUniform1f("linePeriod", (float)std::max(1, std::min(8, p.linePeriod)));
	outShader.setUniform2f("halftoneRot", std::cos(a), std::sin(a));
	outShader.setUniform1f("halftoneCell", (float)std::max(2, std::min(16, p.halftoneCell)));
	outShader.setUniform1f("paletteStops", (float)(p.paletteStops <= 2 ? 2 : 3));
	outShader.setUniform3fv("paletteColor", pal, 3);
	outShader.setUniform1i("lineMode", std::max(0, std::min(2, p.lineProtect)));
	outShader.setUniform1f("lineThreshold", ofClamp(p.lineThreshold, 0.0f, 1.0f));
	outShader.setUniform1f("lineReach", (float)(ds + 1));
	ofPushMatrix();
	ofTranslate(x, y);
	srcQuad.draw();
	ofPopMatrix();
	outShader.end();
	ofPopStyle();
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
}

void DitherFilter::GpuTimer::begin()
{
	started = false;
	if (!checked) {
		checked = true;
		supported = glewIsSupported("GL_ARB_timer_query") || glewIsSupported("GL_VERSION_3_3");
		if (supported)
			glGenQueries(2, ids);
	}
	if (!supported)
		return;
	const int other = 1 - cur;
	if (pending[other]) {
		GLint available = 0;
		glGetQueryObjectiv(ids[other], GL_QUERY_RESULT_AVAILABLE, &available);
		if (available) {
			GLuint64 ns = 0;
			glGetQueryObjectui64v(ids[other], GL_QUERY_RESULT, &ns);
			lastMs = (float)(ns / 1.0e6);
			pending[other] = false;
		}
	}
	if (pending[cur])
		return; // still in flight: skip this measurement (never wait)
	glBeginQuery(GL_TIME_ELAPSED, ids[cur]);
	started = true;
}

void DitherFilter::GpuTimer::end()
{
	if (!started)
		return;
	glEndQuery(GL_TIME_ELAPSED);
	pending[cur] = true;
	cur = 1 - cur;
	started = false;
}

// ------------------------------------------------------------ preset
bool DitherFilter::savePreset()
{
	const Params & p = params;
	ofXml xml;
	auto s = xml.appendChild(PRESET_ROOT);
	s.appendChild("enabled").set(p.enabled ? 1 : 0);
	s.appendChild("mode").set(p.mode);
	s.appendChild("pattern").set(p.pattern);
	s.appendChild("dotSize").set(p.dotSize);
	s.appendChild("levels").set(p.levels);
	s.appendChild("blackPoint").set(p.blackPoint);
	s.appendChild("exposure").set(p.exposure);
	s.appendChild("contrast").set(p.contrast);
	s.appendChild("saturation").set(p.saturation);
	s.appendChild("lineProtect").set(p.lineProtect);
	s.appendChild("lineThreshold").set(p.lineThreshold);
	s.appendChild("linePeriod").set(p.linePeriod);
	s.appendChild("halftoneAngle").set(p.halftoneAngle);
	s.appendChild("halftoneCell").set(p.halftoneCell);
	s.appendChild("palettePreset").set(p.palettePreset);
	s.appendChild("paletteStops").set(p.paletteStops);
	for (int i = 0; i < 3; i++)
		s.appendChild("palette" + ofToString(i)).set(ofToString(p.palette[i].r) + "," + ofToString(p.palette[i].g) + "," + ofToString(p.palette[i].b));
	const bool ok = xml.save(presetFile);
	presetStatus = ok ? "saved " + presetFile : "save failed";
	return ok;
}

bool DitherFilter::loadPreset()
{
	ofXml xml;
	if (!xml.load(presetFile)) { presetStatus = "no preset file"; return false; }
	auto s = xml.getChild(PRESET_ROOT);
	if (!s) { presetStatus = "preset file not recognised"; return false; }
	Params & p = params;
#define LOAD_I(N) if (s.getChild(#N)) p.N = s.getChild(#N).getIntValue();
#define LOAD_F(N) if (s.getChild(#N)) p.N = s.getChild(#N).getFloatValue();
	if (s.getChild("enabled")) p.enabled = s.getChild("enabled").getIntValue() != 0;
	LOAD_I(mode) LOAD_I(pattern) LOAD_I(dotSize) LOAD_I(levels)
	LOAD_F(blackPoint) LOAD_F(exposure) LOAD_F(contrast) LOAD_F(saturation)
	LOAD_I(lineProtect) LOAD_F(lineThreshold) LOAD_I(linePeriod)
	LOAD_F(halftoneAngle) LOAD_I(halftoneCell) LOAD_I(palettePreset) LOAD_I(paletteStops)
#undef LOAD_I
#undef LOAD_F
	for (int i = 0; i < 3; i++) {
		auto c = s.getChild("palette" + ofToString(i));
		if (!c)
			continue;
		const std::vector<std::string> v = ofSplitString(c.getValue(), ",", true, true);
		if (v.size() == 3)
			p.palette[i] = ofFloatColor(ofToFloat(v[0]), ofToFloat(v[1]), ofToFloat(v[2]));
	}
	presetStatus = "loaded " + presetFile;
	return true;
}

// ------------------------------------------------------------ GUI
void DitherFilter::drawGui()
{
	Params & p = params;
	ImGui::PushID("DitherFilter");
	if (ImGui::CollapsingHeader("Dither filter")) {
		ImGui::Text("Screen-space dots over this mode's own output (display only).");
		ImGui::Checkbox("Dither filter on", &p.enabled);
		if (loadState == -1)
			ImGui::Text("Shaders not loaded (bin/data/shaders/dither): drawn unfiltered.");
		if (p.enabled)
			ImGui::Text("GPU ms (capture + filter): %.2f", timer.ms());
		if (ImGui::Button("Save preset"))
			savePreset();
		ImGui::SameLine();
		if (ImGui::Button("Load preset"))
			loadPreset();
		ImGui::SameLine();
		if (ImGui::Button("Defaults")) {
			const bool on = p.enabled;
			p = DEFAULTS;
			p.enabled = on;
		}
		if (!presetStatus.empty())
			ImGui::Text("Preset: %s", presetStatus.c_str());

		static const char * modes[] = { "Hue-preserving", "Per channel", "Palette" };
		ImGui::Combo("Mode", &p.mode, modes, 3);
		static const char * patterns[] = { "Bayer 4x4", "Bayer 8x8", "Line screen (horizontal)", "Halftone (clustered dot)" };
		ImGui::Combo("Pattern", &p.pattern, patterns, 4);
		if (p.pattern == PATTERN_LINES)
			ImGui::SliderInt("Line period (px)", &p.linePeriod, 1, 8);
		if (p.pattern == PATTERN_HALFTONE) {
			ImGui::SliderFloat("Halftone angle (deg)", &p.halftoneAngle, 0.0f, 90.0f);
			ImGui::SliderInt("Halftone cell (dots)", &p.halftoneCell, 2, 16);
		}
		ImGui::SliderInt("Dot size (px)", &p.dotSize, 1, 8);
		ImGui::SliderInt("Levels", &p.levels, 2, 16);
		ImGui::SliderFloat("Black point", &p.blackPoint, 0.0f, 0.5f);

		ImGui::Separator();
		ImGui::Text("Pre-filter (before the dots; hue kept)");
		ImGui::SliderFloat("Exposure", &p.exposure, 0.25f, 4.0f);
		ImGui::SliderFloat("Contrast", &p.contrast, 0.0f, 3.0f);
		ImGui::SliderFloat("Saturation", &p.saturation, 0.0f, 3.0f);

		ImGui::Separator();
		static const char * protect[] = { "Off", "Bright lines", "Bright and dark lines" };
		ImGui::Combo("Line protection", &p.lineProtect, protect, 3);
		ImGui::SliderFloat("Line threshold", &p.lineThreshold, 0.02f, 0.6f);
		ImGui::Text("(pixels this much brighter, or darker, than both sides: undithered)");

		if (p.mode == MODE_PALETTE) {
			ImGui::Separator();
			static const char * presets[] = { "Gold", "Night", "Green" };
			if (ImGui::Combo("Palette preset", &p.palettePreset, presets, 3))
				applyPalettePreset(p, p.palettePreset);
			ImGui::SliderInt("Palette stops", &p.paletteStops, 2, 3);
			ImGui::ColorEdit3("Dark", &p.palette[0].r);
			ImGui::ColorEdit3("Mid", &p.palette[1].r);
			if (p.paletteStops >= 3)
				ImGui::ColorEdit3("Light", &p.palette[2].r);
		}
	}
	ImGui::PopID();
}
