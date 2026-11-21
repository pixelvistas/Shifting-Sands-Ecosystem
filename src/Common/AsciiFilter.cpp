#include "AsciiFilter.h"
#include "ofxImGui.h"

const int AsciiFilter::CELL_W[AsciiFilter::CELL_PRESETS] = { 6, 7, 8, 10 };
const int AsciiFilter::CELL_H[AsciiFilter::CELL_PRESETS] = { 10, 10, 12, 16 };

// Fill pools, dark to bright (chosen by the cell's luminance in 4 equal bins).
const char * AsciiFilter::POOLS[4] = { "1il!;:", "rtfjcvxz=+", "aeoun0{}[]<>", "08B$#&%Q" };
const char * AsciiFilter::FONT_FILE = "fonts/JetBrainsMono-Regular.ttf";

// Defaults (DEVLOG, 2026-11-21; chosen on the headless Sandbox preview,
// tools/sandbox_ascii_preview, to be confirmed on the sand).
static AsciiFilter::Params makeDefaults()
{
	AsciiFilter::Params p;
	p.enabled = true;
	p.cellPreset = AsciiFilter::CELLS_7x10;
	p.blackThreshold = 0.04f;
	p.minCoverage = 0.2f;
	p.edgeFloor = 0.85f;
	p.edgeOneSided = false;
	p.shimmerRate[0] = 0.02f;   // Sandbox: bare land, nearly still
	p.shimmerRate[1] = 0.2f;    //          plants, slow
	p.shimmerRate[2] = 1.5f;    //          water, fastest
	p.trailAmount = 0.5f;
	p.glowTight = 0.75f;
	p.glowWide = 0.45f;
	p.glowFloor = 0.04f;
	p.glowOutside = false;
	p.markThreshold = 0.25f;
	p.exposure = 1.0f;
	p.saturation = 1.0f;
	return p;
}
const AsciiFilter::Params AsciiFilter::DEFAULTS = makeDefaults();

static const char * PRESET_ROOT = "ASCIIFILTER";

// ------------------------------------------------------------ GL helpers
static void allocateRect(ofFbo & fbo, int w, int h, GLint internalFormat, bool linear)
{
	ofFboSettings s;
	s.width = std::max(1, w);
	s.height = std::max(1, h);
	s.internalformat = internalFormat;
	s.textureTarget = GL_TEXTURE_RECTANGLE_ARB;
	s.minFilter = linear ? GL_LINEAR : GL_NEAREST;
	s.maxFilter = linear ? GL_LINEAR : GL_NEAREST;
	s.wrapModeHorizontal = GL_CLAMP_TO_EDGE;
	s.wrapModeVertical = GL_CLAMP_TO_EDGE;
	s.useDepth = false;
	s.numSamples = 0;
	fbo.allocate(s);
}

// A w x h rectangle whose texture coordinates are its own pixel coordinates.
static void drawPixelRect(float w, float h)
{
	ofMesh q;
	q.setMode(OF_PRIMITIVE_TRIANGLE_STRIP);
	q.addVertex(glm::vec3(0, 0, 0)); q.addTexCoord(glm::vec2(0, 0));
	q.addVertex(glm::vec3(w, 0, 0)); q.addTexCoord(glm::vec2(w, 0));
	q.addVertex(glm::vec3(0, h, 0)); q.addTexCoord(glm::vec2(0, h));
	q.addVertex(glm::vec3(w, h, 0)); q.addTexCoord(glm::vec2(w, h));
	q.draw();
}

static bool isRect(const ofTexture & t)
{
	return t.isAllocated() && t.getTextureData().textureTarget == GL_TEXTURE_RECTANGLE_ARB;
}

bool AsciiFilter::loadShaders()
{
#ifdef TARGET_OPENGLES
	loadState = -1;
	return false;
#else
	const std::string dir = ofIsGLProgrammableRenderer() ? "shaders/ascii/GL3/" : "shaders/ascii/GL2/";
	const std::string v = dir + "ascii.vert";
	const bool ok = cellsShader.load(v, dir + "asciiCells.frag")
		&& partsShader.load(v, dir + "asciiParts.frag")
		&& marksShader.load(v, dir + "asciiMarks.frag")
		&& markWriteShader.load(v, dir + "asciiMarkWrite.frag")
		&& decideShader.load(v, dir + "asciiDecide.frag")
		&& glyphsShader.load(v, dir + "asciiGlyphs.frag")
		&& trailShader.load(v, dir + "asciiTrail.frag")
		&& downShader.load(v, dir + "asciiDown.frag")
		&& blurShader.load(v, dir + "asciiBlur.frag")
		&& outShader.load(v, dir + "asciiOut.frag");
	if (!ok)
		ofLogError("AsciiFilter") << "could not load the shaders in " << dir;
	loadState = ok ? 1 : -1;
	return ok;
#endif
}

bool AsciiFilter::isReady()
{
	if (loadState == 0)
		loadShaders();
	return loadState == 1 && atlasState != -1;
}

// Ink bounds of the printable characters relative to the baseline (top < 0)
// and the advance, at the font's loaded size.
static void measureFont(const ofTrueTypeFont & f, float & top, float & bottom, float & advance)
{
	top = 0.0f;
	bottom = 0.0f;
	for (int c = 33; c <= 126; c++) {
		const ofRectangle r = f.getStringBoundingBox(std::string(1, (char)c), 0, 0);
		top = std::min(top, r.y);
		bottom = std::max(bottom, r.y + r.height);
	}
	advance = f.getStringBoundingBox("MM", 0, 0).width - f.getStringBoundingBox("M", 0, 0).width;
}

// Atlas: 16 x 6 slots of cw x ch pixels, slot i = character 32 + i, white
// on transparent black (R = coverage). Baked once per cell size.
bool AsciiFilter::ensureAtlas(int cw, int ch)
{
	if (atlasState == -1)
		return false;
	if (atlasState == 1 && cw == atlasCw && ch == atlasCh)
		return true;
	const int REF = 64;
	ofTrueTypeFont probe;
	if (!probe.load(FONT_FILE, REF, true, false, false, 0.3f, 72)) {
		ofLogError("AsciiFilter") << "could not load " << FONT_FILE;
		atlasState = -1;
		return false;
	}
	float top, bottom, adv;
	measureFont(probe, top, bottom, adv);
	if (adv < 1.0f)
		adv = 0.6f * REF;
	const float inkH = std::max(1.0f, bottom - top);
	const int size = std::max(4, (int)std::floor(REF * std::min((float)cw / adv, (float)ch / inkH)));
	ofTrueTypeFont font;
	if (!font.load(FONT_FILE, size, true, false, false, 0.3f, 72)) {
		ofLogError("AsciiFilter") << "could not load " << FONT_FILE << " at " << size << " px";
		atlasState = -1;
		return false;
	}
	measureFont(font, top, bottom, adv);
	const float baseline = std::round(0.5f * (ch - (bottom - top)) - top);
	const float xOff = std::round(0.5f * (cw - adv));

	allocateRect(atlasFbo, 16 * cw, 6 * ch, GL_RGBA, false);
	atlasFbo.begin();
	ofClear(0, 0, 0, 0);
	ofPushStyle();
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
	ofSetColor(255);
	for (int c = 33; c <= 126; c++) {
		const int slot = c - 32;
		font.drawString(std::string(1, (char)c), (float)((slot % 16) * cw) + xOff, (float)((slot / 16) * ch) + baseline);
	}
	ofPopStyle();
	atlasFbo.end();
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
	atlasCw = cw;
	atlasCh = ch;
	atlasState = 1;
	ofLogNotice("AsciiFilter") << "font atlas " << cw << " x " << ch << " px cells, font " << size << " px";
	return true;
}

void AsciiFilter::ensureTargets(int w, int h, int cw, int ch)
{
	const int gw = (w + cw - 1) / cw, gh = (h + ch - 1) / ch;
	if (gw != gridW || gh != gridH || !cellFbo.isAllocated()) {
		allocateRect(cellFbo, gw, gh, GL_RGBA16F, false);
		allocateRect(partFbo, gw, gh, GL_RGBA, false);
		allocateRect(markCellFbo, gw, gh, GL_RGBA, false);
		allocateRect(decideFbo, gw, gh, GL_RGBA16F, false);
		gridW = gw;
		gridH = gh;
	}
	if (w != outW || h != outH || !frameFbo.isAllocated()) {
		allocateRect(frameFbo, w, h, GL_RGBA16F, false);
		for (int i = 0; i < 2; i++)
			allocateRect(trailFbo[i], w, h, GL_RGBA16F, false);
		const int tw = (w + 3) / 4, th = (h + 3) / 4;
		for (int i = 0; i < 2; i++) {
			allocateRect(tightFbo[i], tw, th, GL_RGBA16F, true);
			allocateRect(wideFbo[i], (tw + 1) / 2, (th + 1) / 2, GL_RGBA16F, true);
		}
		if (marksFbo.isAllocated() && ((int)marksFbo.getWidth() != w || (int)marksFbo.getHeight() != h))
			marksThisFrame = false; // marks drawn for another size
		outW = w;
		outH = h;
		trailValid = false;
	}
}

// ------------------------------------------------------------ marks
void AsciiFilter::beginMarks(int w, int h)
{
	marksThisFrame = false;
	if (!isReady() || w <= 0 || h <= 0)
		return;
	if (!timing) {
		timer.begin();
		timing = true;
	}
	if (!marksFbo.isAllocated() || (int)marksFbo.getWidth() != w || (int)marksFbo.getHeight() != h)
		allocateRect(marksFbo, w, h, GL_RGBA, false);
	marksFbo.begin();
	ofClear(0, 0, 0, 0);
	marksFbo.end();
	marksThisFrame = true;
}

void AsciiFilter::addMarks(const ofTexture & layer, char glyph, bool premultiplied)
{
	if (!marksThisFrame || !isRect(layer))
		return;
	const int g = (int)(unsigned char)glyph;
	const float w = marksFbo.getWidth(), h = marksFbo.getHeight();
	ofPushStyle();
	ofEnableBlendMode(OF_BLENDMODE_DISABLED);
	marksFbo.begin();
	markWriteShader.begin();
	markWriteShader.setUniformTexture("srcTex", layer, 0);
	markWriteShader.setUniform2f("srcScale", layer.getWidth() / w, layer.getHeight() / h);
	markWriteShader.setUniform1i("premultiplied", premultiplied ? 1 : 0);
	markWriteShader.setUniform1f("glyph", (float)((g >= 33 && g <= 126) ? g : '*'));
	markWriteShader.setUniform1f("threshold", ofClamp(params.markThreshold, 0.0f, 1.0f));
	drawPixelRect(w, h);
	markWriteShader.end();
	marksFbo.end();
	ofPopStyle();
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
}

// ------------------------------------------------------------ draw
bool AsciiFilter::draw(const ofTexture & colour, const ofTexture & parts, float x, float y)
{
	const int w = (int)colour.getWidth(), h = (int)colour.getHeight();
	if (!isReady() || !isRect(colour) || !isRect(parts) || w <= 0 || h <= 0) {
		marksThisFrame = false;
		return false;
	}
	const Params & p = params;
	const int cw = cellW(), ch = cellH();
	if (!ensureAtlas(cw, ch)) {
		marksThisFrame = false;
		return false;
	}
	if (!timing) {
		timer.begin();
		timing = true;
	}
	ensureTargets(w, h, cw, ch);
	const bool marksOn = marksThisFrame && (int)marksFbo.getWidth() == w && (int)marksFbo.getHeight() == h;

	ofPushStyle();
	ofEnableBlendMode(OF_BLENDMODE_DISABLED);
	ofSetColor(255);

	// 1. Coverage and colour per cell.
	cellFbo.begin();
	cellsShader.begin();
	cellsShader.setUniformTexture("srcTex", colour, 0);
	cellsShader.setUniform2f("srcSize", (float)w, (float)h);
	cellsShader.setUniform2f("cellSize", (float)cw, (float)ch);
	cellsShader.setUniform1f("blackThreshold", ofClamp(p.blackThreshold, 0.0f, 1.0f));
	cellsShader.setUniform1f("exposure", ofClamp(p.exposure, 0.0f, 8.0f));
	cellsShader.setUniform1f("saturation", ofClamp(p.saturation, 0.0f, 4.0f));
	drawPixelRect((float)gridW, (float)gridH);
	cellsShader.end();
	cellFbo.end();

	// 2. Dominant part per cell (parts at the same size as the colour, or scaled).
	partFbo.begin();
	partsShader.begin();
	partsShader.setUniformTexture("partsTex", parts, 0);
	partsShader.setUniform2f("srcSize", parts.getWidth(), parts.getHeight());
	partsShader.setUniform2f("cellSize", (float)cw * parts.getWidth() / w, (float)ch * parts.getHeight() / h);
	drawPixelRect((float)gridW, (float)gridH);
	partsShader.end();
	partFbo.end();

	// 3. Brightest mark per cell.
	if (marksOn) {
		markCellFbo.begin();
		marksShader.begin();
		marksShader.setUniformTexture("marksTex", marksFbo.getTexture(), 0);
		marksShader.setUniform2f("srcSize", (float)w, (float)h);
		marksShader.setUniform2f("cellSize", (float)cw, (float)ch);
		drawPixelRect((float)gridW, (float)gridH);
		marksShader.end();
		markCellFbo.end();
	}

	// 4. The character of each cell.
	float glyphs[48] = { 0 }, start[4] = { 0 }, len[4] = { 0 };
	int n = 0;
	for (int i = 0; i < 4; i++) {
		start[i] = (float)n;
		for (const char * c = POOLS[i]; *c && n < 48; c++)
			glyphs[n++] = (float)(unsigned char)*c;
		len[i] = (float)n - start[i];
	}
	float rates[3];
	for (int i = 0; i < 3; i++)
		rates[i] = ofClamp(p.shimmerRate[i], 0.0f, 30.0f);
	decideFbo.begin();
	decideShader.begin();
	decideShader.setUniformTexture("cellTex", cellFbo.getTexture(), 0);
	decideShader.setUniformTexture("partTex", partFbo.getTexture(), 1);
	decideShader.setUniformTexture("markTex", (marksOn ? markCellFbo : partFbo).getTexture(), 2);
	decideShader.setUniform1i("marksOn", marksOn ? 1 : 0);
	decideShader.setUniform2f("gridSize", (float)gridW, (float)gridH);
	decideShader.setUniform1f("minCoverage", ofClamp(p.minCoverage, 0.0f, 1.0f));
	decideShader.setUniform1f("edgeFloor", ofClamp(p.edgeFloor, 0.0f, 1.0f));
	decideShader.setUniform1i("oneSided", p.edgeOneSided ? 1 : 0);
	decideShader.setUniform1fv("poolGlyph", glyphs, 48);
	decideShader.setUniform1fv("poolStart", start, 4);
	decideShader.setUniform1fv("poolLen", len, 4);
	decideShader.setUniform1fv("shimmerRate", rates, 3);
	decideShader.setUniform1f("time", ofGetElapsedTimef());
	drawPixelRect((float)gridW, (float)gridH);
	decideShader.end();
	decideFbo.end();

	// 5. Characters from the atlas.
	frameFbo.begin();
	glyphsShader.begin();
	glyphsShader.setUniformTexture("decideTex", decideFbo.getTexture(), 0);
	glyphsShader.setUniformTexture("atlasTex", atlasFbo.getTexture(), 1);
	glyphsShader.setUniform2f("cellSize", (float)cw, (float)ch);
	drawPixelRect((float)w, (float)h);
	glyphsShader.end();
	frameFbo.end();

	// 6. Trails (feedback): max(previous x amount, this frame).
	const float trail = ofClamp(p.trailAmount, 0.0f, 0.98f);
	ofFbo * result = &frameFbo;
	if (trail > 0.0f) {
		ofFbo & prev = trailFbo[trailCur];
		if (!trailValid) {
			prev.begin();
			ofClear(0, 0, 0, 0);
			prev.end();
		}
		ofFbo & next = trailFbo[1 - trailCur];
		next.begin();
		trailShader.begin();
		trailShader.setUniformTexture("curTex", frameFbo.getTexture(), 0);
		trailShader.setUniformTexture("prevTex", prev.getTexture(), 1);
		trailShader.setUniform1f("amount", trail);
		drawPixelRect((float)w, (float)h);
		trailShader.end();
		next.end();
		trailCur = 1 - trailCur;
		trailValid = true;
		result = &next;
	} else {
		trailValid = false;
	}

	// 7. Double glow: tight (1/4) and wide (1/8, from the tight one).
	const bool glowOn = p.glowTight > 0.0f || p.glowWide > 0.0f;
	if (glowOn) {
		auto down = [&](const ofFbo & src, ofFbo & dst, float factor) {
			dst.begin();
			downShader.begin();
			downShader.setUniformTexture("srcTex", src.getTexture(), 0);
			downShader.setUniform2f("srcSize", src.getWidth(), src.getHeight());
			downShader.setUniform1f("factor", factor);
			drawPixelRect(dst.getWidth(), dst.getHeight());
			downShader.end();
			dst.end();
		};
		auto blur = [&](const ofFbo & src, ofFbo & dst, float dx, float dy, float sigma) {
			dst.begin();
			blurShader.begin();
			blurShader.setUniformTexture("srcTex", src.getTexture(), 0);
			blurShader.setUniform2f("dir", dx, dy);
			blurShader.setUniform1f("sigma", sigma);
			drawPixelRect(dst.getWidth(), dst.getHeight());
			blurShader.end();
			dst.end();
		};
		down(*result, tightFbo[0], 4.0f);
		blur(tightFbo[0], tightFbo[1], 1.0f, 0.0f, 1.5f);
		blur(tightFbo[1], tightFbo[0], 0.0f, 1.0f, 1.5f);
		down(tightFbo[0], wideFbo[0], 2.0f);
		blur(wideFbo[0], wideFbo[1], 1.0f, 0.0f, 2.0f);
		blur(wideFbo[1], wideFbo[0], 0.0f, 1.0f, 2.0f);
	}

	// Output, into whatever is bound (the projector window).
	outShader.begin();
	outShader.setUniformTexture("frameTex", result->getTexture(), 0);
	outShader.setUniformTexture("tightTex", tightFbo[0].getTexture(), 1);
	outShader.setUniformTexture("wideTex", wideFbo[0].getTexture(), 2);
	outShader.setUniform1i("glowOn", glowOn ? 1 : 0);
	outShader.setUniform1f("tightAmount", std::max(0.0f, p.glowTight));
	outShader.setUniform1f("wideAmount", std::max(0.0f, p.glowWide));
	outShader.setUniform1f("blackFloor", ofClamp(p.glowFloor, 0.0f, 0.5f));
	outShader.setUniform1i("glowOutside", p.glowOutside ? 1 : 0);
	ofPushMatrix();
	ofTranslate(x, y);
	drawPixelRect((float)w, (float)h);
	ofPopMatrix();
	outShader.end();

	ofPopStyle();
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
	timer.end();
	timing = false;
	marksThisFrame = false;
	return true;
}

// ------------------------------------------------------------ GPU timer (as DitherFilter's)
void AsciiFilter::GpuTimer::begin()
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

void AsciiFilter::GpuTimer::end()
{
	if (!started)
		return;
	glEndQuery(GL_TIME_ELAPSED);
	pending[cur] = true;
	cur = 1 - cur;
	started = false;
}

// ------------------------------------------------------------ preset
bool AsciiFilter::savePreset()
{
	const Params & p = params;
	ofXml xml;
	auto s = xml.appendChild(PRESET_ROOT);
	s.appendChild("enabled").set(p.enabled ? 1 : 0);
	s.appendChild("cellPreset").set(p.cellPreset);
	s.appendChild("blackThreshold").set(p.blackThreshold);
	s.appendChild("minCoverage").set(p.minCoverage);
	s.appendChild("edgeFloor").set(p.edgeFloor);
	s.appendChild("edgeOneSided").set(p.edgeOneSided ? 1 : 0);
	for (int i = 0; i < 3; i++)
		s.appendChild("shimmerRate" + ofToString(i)).set(p.shimmerRate[i]);
	s.appendChild("trailAmount").set(p.trailAmount);
	s.appendChild("glowTight").set(p.glowTight);
	s.appendChild("glowWide").set(p.glowWide);
	s.appendChild("glowFloor").set(p.glowFloor);
	s.appendChild("glowOutside").set(p.glowOutside ? 1 : 0);
	s.appendChild("markThreshold").set(p.markThreshold);
	s.appendChild("exposure").set(p.exposure);
	s.appendChild("saturation").set(p.saturation);
	const bool ok = xml.save(presetFile);
	presetStatus = ok ? "saved " + presetFile : "save failed";
	return ok;
}

bool AsciiFilter::loadPreset()
{
	ofXml xml;
	if (!xml.load(presetFile)) { presetStatus = "no preset file"; return false; }
	auto s = xml.getChild(PRESET_ROOT);
	if (!s) { presetStatus = "preset file not recognised"; return false; }
	Params & p = params;
#define LOAD_I(N) if (s.getChild(#N)) p.N = s.getChild(#N).getIntValue();
#define LOAD_F(N) if (s.getChild(#N)) p.N = s.getChild(#N).getFloatValue();
#define LOAD_B(N) if (s.getChild(#N)) p.N = s.getChild(#N).getIntValue() != 0;
	LOAD_B(enabled) LOAD_I(cellPreset) LOAD_F(blackThreshold) LOAD_F(minCoverage)
	LOAD_F(edgeFloor) LOAD_B(edgeOneSided) LOAD_F(trailAmount) LOAD_F(glowTight)
	LOAD_F(glowWide) LOAD_F(glowFloor) LOAD_B(glowOutside) LOAD_F(markThreshold)
	LOAD_F(exposure) LOAD_F(saturation)
#undef LOAD_I
#undef LOAD_F
#undef LOAD_B
	for (int i = 0; i < 3; i++)
		if (s.getChild("shimmerRate" + ofToString(i)))
			p.shimmerRate[i] = s.getChild("shimmerRate" + ofToString(i)).getFloatValue();
	p.cellPreset = clampPreset(p.cellPreset);
	presetStatus = "loaded " + presetFile;
	return true;
}

// ------------------------------------------------------------ GUI
void AsciiFilter::drawGui()
{
	Params & p = params;
	ImGui::PushID("AsciiFilter");
	if (ImGui::CollapsingHeader("ASCII filter")) {
		ImGui::Text("Characters over this mode's own output (display only).");
		ImGui::Text("Visual reference: @dwcminh.ascii (look only, no code).");
		ImGui::Checkbox("ASCII filter on", &p.enabled);
		if (loadState == -1)
			ImGui::Text("Shaders not loaded (bin/data/shaders/ascii): drawn unfiltered.");
		if (atlasState == -1)
			ImGui::Text("Font not loaded (bin/data/%s): drawn unfiltered.", FONT_FILE);
		if (p.enabled)
			ImGui::Text("GPU ms (marks + filter): %.2f", timer.ms());
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

		static const char * cells[] = { "6 x 10 px", "7 x 10 px", "8 x 12 px", "10 x 16 px" };
		ImGui::Combo("Cell size", &p.cellPreset, cells, CELL_PRESETS);
		p.cellPreset = clampPreset(p.cellPreset);
		ImGui::SliderFloat("Black threshold", &p.blackThreshold, 0.0f, 0.3f);
		ImGui::SliderFloat("Minimum coverage", &p.minCoverage, 0.0f, 1.0f);
		ImGui::Text("(cells under it are empty: exact black, the sand shows)");
		ImGui::SliderFloat("Outline brightness floor", &p.edgeFloor, 0.0f, 1.0f);
		ImGui::Checkbox("Outline part boundaries on one side only", &p.edgeOneSided);
		ImGui::SliderFloat("Mark threshold", &p.markThreshold, 0.0f, 0.5f);

		ImGui::Separator();
		ImGui::Text("Fill shimmer (letter steps per second per cell)");
		for (int i = 0; i < 3; i++)
			ImGui::SliderFloat((classNames[i] + " shimmer").c_str(), &p.shimmerRate[i], 0.0f, 4.0f, "%.3f");

		ImGui::Separator();
		ImGui::SliderFloat("Trail amount (0 = off)", &p.trailAmount, 0.0f, 0.95f);
		ImGui::SliderFloat("Glow, tight (1/4 res)", &p.glowTight, 0.0f, 2.0f);
		ImGui::SliderFloat("Glow, wide (1/8 res)", &p.glowWide, 0.0f, 2.0f);
		ImGui::SliderFloat("Glow black floor", &p.glowFloor, 0.0f, 0.3f);
		ImGui::Checkbox("Glow lights empty cells", &p.glowOutside);
		ImGui::Text("(off: empty cells stay exact black)");

		ImGui::Separator();
		ImGui::Text("Pre-filter (before the filter; hue kept)");
		ImGui::SliderFloat("Exposure", &p.exposure, 0.25f, 4.0f);
		ImGui::SliderFloat("Saturation", &p.saturation, 0.0f, 3.0f);
	}
	ImGui::PopID();
}
