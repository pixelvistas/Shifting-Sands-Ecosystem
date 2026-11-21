#include "AsciiFilter.h"
#include "ofxImGui.h"
#include <cstring>

const int AsciiFilter::CELL_W[AsciiFilter::CELL_PRESETS] = { 6, 7, 8, 10 };
const int AsciiFilter::CELL_H[AsciiFilter::CELL_PRESETS] = { 10, 10, 12, 16 };

// Fill pools, dark to bright; the pool is chosen by brightness and each is
// repeated to 12 entries (the reference's pools, by @dwcminh.ascii).
const char * AsciiFilter::POOLS[4] = { "1il!;:", "rtfjcvxz=+", "aeoun0{}[]<>", "08B$#&%Q" };
const char * AsciiFilter::FONT_FILE = "fonts/JetBrainsMono-Regular.ttf";

// The reference's font list (betta_fish.py: Consolas, Menlo, DejaVu Sans
// Mono, Courier New): system fonts loaded at run time if installed, never
// bundled. Then the bundled font.
static const char * FONT_SYSTEM[] = { "C:/Windows/Fonts/consola.ttf", "/System/Library/Fonts/Menlo.ttc" };

// Defaults (DEVLOG, 2026-11-21): the reference's values where it has one
// (betta_fish.py, @dwcminh.ascii); the mark threshold, glow mask and
// pre-filter are ours.
static AsciiFilter::Params makeDefaults()
{
	AsciiFilter::Params p;
	p.enabled = true;
	p.cellPreset = AsciiFilter::CELLS_7x10;
	p.blackThreshold = 0.04f;
	p.minCoverage = 0.0f;       // reference: any covered pixel draws the cell
	p.edgeFloor = 0.85f;
	p.edgeOneSided = true;
	p.edgeSideOnly = true;
	p.fillFloor = 0.45f;
	p.fillStart = 0.42f;
	p.fillScale = 4.6f;
	p.fontPx = 0;               // auto: 1.1 x cell height
	p.glyphCentring = AsciiFilter::GLYPH_CENTRE_LINE_BOX;
	p.fontChoice = AsciiFilter::FONT_REFERENCE_ORDER;
	p.shimmerRate[0] = 4.0f;    // reference: 4 steps a second for every cell
	p.shimmerRate[1] = 4.0f;    // (Sandbox: bare land, plants, water)
	p.shimmerRate[2] = 4.0f;
	p.trailAmount = 0.5f;
	p.glowTight = 0.75f;
	p.glowWide = 0.45f;
	p.glowFloor = 0.0f;         // reference: no floor (the mask keeps empty cells black)
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

// Font metrics at the loaded size: ink bounds of the printable characters
// relative to the baseline (top < 0), the advance, and the line box
// (ascender, descender < 0).
struct FontMetrics { float top, bottom, advance, ascender, descender; };

static FontMetrics measureFont(const ofTrueTypeFont & f)
{
	FontMetrics m;
	m.top = 0.0f;
	m.bottom = 0.0f;
	for (int c = 33; c <= 126; c++) {
		const ofRectangle r = f.getStringBoundingBox(std::string(1, (char)c), 0, 0);
		m.top = std::min(m.top, r.y);
		m.bottom = std::max(m.bottom, r.y + r.height);
	}
	m.advance = f.getStringBoundingBox("MM", 0, 0).width - f.getStringBoundingBox("M", 0, 0).width;
	m.ascender = f.getAscenderHeight();
	m.descender = f.getDescenderHeight();
	return m;
}

static int floorDiv2(float v)
{
	return (int)std::floor(v / 2.0f);
}

// Atlas: 16 x 6 slots, slot i = character 32 + i, white on transparent
// black (R = coverage). Each slot is the cw x ch cell with a margin of
// ch pixels all round (pad); the glyph pass reads only the cell, so
// whatever a character draws outside its cell lands in the margin and is
// cropped, as in the reference (and never spills into a neighbour's cell).
// Line box (reference): font size px (default 1.1 x ch), each character
// placed at ((cw - advance) / 2, (ch - line height) / 2), rounded down, with
// its baseline at the ascender below that. Ink box: as before this pass, a
// size and placement that fit the ink of all 95 characters in the cell.
bool AsciiFilter::ensureAtlas(int cw, int ch, int px, int centring, int fontChoice)
{
	if (atlasState == -1)
		return false;
	if (atlasState == 1 && cw == atlasCw && ch == atlasCh && px == atlasPx && centring == atlasCentring && fontChoice == atlasFontChoice)
		return true;
	std::vector<std::string> files;
	if (fontChoice == FONT_REFERENCE_ORDER)
		for (const char * f : FONT_SYSTEM)
			if (ofFile::doesFileExist(f, false))
				files.push_back(f);
	files.push_back(FONT_FILE);
	ofTrueTypeFont font;
	std::string used;
	int size = std::max(4, px);
	for (const std::string & f : files) {
		if (centring == GLYPH_CENTRE_INK_BOX) {
			const int REF = 64;
			ofTrueTypeFont probe;
			if (!probe.load(f, REF, true, false, false, 0.3f, 72))
				continue;
			FontMetrics m = measureFont(probe);
			const float adv = (m.advance >= 1.0f) ? m.advance : 0.6f * REF;
			size = std::max(4, (int)std::floor(REF * std::min((float)cw / adv, (float)ch / std::max(1.0f, m.bottom - m.top))));
		}
		if (font.load(f, size, true, false, false, 0.3f, 72)) {
			used = f;
			break;
		}
	}
	if (used.empty()) {
		ofLogError("AsciiFilter") << "could not load " << FONT_FILE;
		atlasState = -1;
		return false;
	}
	const FontMetrics m = measureFont(font);
	float baseline, xOff;
	if (centring == GLYPH_CENTRE_INK_BOX) {
		baseline = std::round(0.5f * (ch - (m.bottom - m.top)) - m.top);
		xOff = std::round(0.5f * (cw - m.advance));
	} else {
		const float asc = std::round(m.ascender), lineH = std::round(m.ascender - m.descender);
		baseline = (float)floorDiv2(ch - lineH) + asc;
		xOff = (float)floorDiv2(cw - std::round(m.advance));
	}

	const int pad = ch, sw = cw + 2 * pad, sh = ch + 2 * pad;
	allocateRect(atlasFbo, 16 * sw, 6 * sh, GL_RGBA, false);
	atlasFbo.begin();
	ofClear(0, 0, 0, 0);
	ofPushStyle();
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
	ofSetColor(255);
	for (int c = 33; c <= 126; c++) {
		const int slot = c - 32;
		const int sx = (slot % 16) * sw + pad, sy = (slot / 16) * sh + pad;   // the cell inside the slot
		font.drawString(std::string(1, (char)c), (float)sx + xOff, (float)sy + baseline);
	}
	ofPopStyle();
	atlasFbo.end();
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
	atlasCw = cw;
	atlasCh = ch;
	atlasPx = px;
	atlasCentring = centring;
	atlasFontChoice = fontChoice;
	atlasFont = used;
	atlasState = 1;
	ofLogNotice("AsciiFilter") << "font atlas " << cw << " x " << ch << " px cells, " << used << " at " << size << " px";
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
	if (!ensureAtlas(cw, ch, fontPxFor(ch), std::max(0, std::min(1, p.glyphCentring)), std::max(0, std::min(1, p.fontChoice)))) {
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

	// 1. The part of each cell (parts at the same size as the colour, or scaled).
	partFbo.begin();
	partsShader.begin();
	partsShader.setUniformTexture("partsTex", parts, 0);
	partsShader.setUniform2f("srcSize", parts.getWidth(), parts.getHeight());
	partsShader.setUniform2f("cellSize", (float)cw * parts.getWidth() / w, (float)ch * parts.getHeight() / h);
	drawPixelRect((float)gridW, (float)gridH);
	partsShader.end();
	partFbo.end();

	// 2. Coverage and colour per cell (colour from the cell's own part).
	cellFbo.begin();
	cellsShader.begin();
	cellsShader.setUniformTexture("srcTex", colour, 0);
	cellsShader.setUniformTexture("partsTex", parts, 1);
	cellsShader.setUniformTexture("cellPartTex", partFbo.getTexture(), 2);
	cellsShader.setUniform2f("srcSize", (float)w, (float)h);
	cellsShader.setUniform2f("partsScale", parts.getWidth() / w, parts.getHeight() / h);
	cellsShader.setUniform2f("cellSize", (float)cw, (float)ch);
	cellsShader.setUniform1f("blackThreshold", ofClamp(p.blackThreshold, 0.0f, 1.0f));
	cellsShader.setUniform1f("exposure", ofClamp(p.exposure, 0.0f, 8.0f));
	cellsShader.setUniform1f("saturation", ofClamp(p.saturation, 0.0f, 4.0f));
	drawPixelRect((float)gridW, (float)gridH);
	cellsShader.end();
	cellFbo.end();

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
	float fill[48];   // 4 pools x 12 entries, each pool repeated
	for (int i = 0; i < 4; i++) {
		const size_t len = std::max<size_t>(1, std::strlen(POOLS[i]));
		for (int k = 0; k < 12; k++)
			fill[i * 12 + k] = (float)(unsigned char)POOLS[i][k % len];
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
	decideShader.setUniform1i("sideOnly", p.edgeSideOnly ? 1 : 0);
	decideShader.setUniform1fv("fillGlyph", fill, 48);
	decideShader.setUniform1f("fillFloor", ofClamp(p.fillFloor, 0.0f, 1.0f));
	decideShader.setUniform1f("fillStart", p.fillStart);
	decideShader.setUniform1f("fillScale", std::max(0.0f, p.fillScale));
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
	glyphsShader.setUniform1f("atlasPad", (float)atlasCh);   // = ensureAtlas()'s pad
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
		auto blur = [&](const ofFbo & src, ofFbo & dst, float dx, float dy, float sigma, float reach) {
			dst.begin();
			blurShader.begin();
			blurShader.setUniformTexture("srcTex", src.getTexture(), 0);
			blurShader.setUniform2f("dir", dx, dy);
			blurShader.setUniform1f("sigma", sigma);
			blurShader.setUniform1f("reach", reach);
			drawPixelRect(dst.getWidth(), dst.getHeight());
			blurShader.end();
			dst.end();
		};
		// As the reference: shrink to 1/4 (box), blur (pygame-ce
		// gaussian_blur radius 2: sigma 1.78, taps +-4); shrink that to 1/8,
		// blur (radius 3: sigma 2.40, taps +-5). Sigmas measured on an impulse.
		down(*result, tightFbo[0], 4.0f);
		blur(tightFbo[0], tightFbo[1], 1.0f, 0.0f, 1.78f, 4.0f);
		blur(tightFbo[1], tightFbo[0], 0.0f, 1.0f, 1.78f, 4.0f);
		down(tightFbo[0], wideFbo[0], 2.0f);
		blur(wideFbo[0], wideFbo[1], 1.0f, 0.0f, 2.40f, 5.0f);
		blur(wideFbo[1], wideFbo[0], 0.0f, 1.0f, 2.40f, 5.0f);
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
	s.appendChild("edgeSideOnly").set(p.edgeSideOnly ? 1 : 0);
	s.appendChild("fillFloor").set(p.fillFloor);
	s.appendChild("fillStart").set(p.fillStart);
	s.appendChild("fillScale").set(p.fillScale);
	s.appendChild("fontPx").set(p.fontPx);
	s.appendChild("glyphCentring").set(p.glyphCentring);
	s.appendChild("fontChoice").set(p.fontChoice);
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
	LOAD_B(edgeSideOnly) LOAD_F(fillFloor) LOAD_F(fillStart) LOAD_F(fillScale)
	LOAD_I(fontPx) LOAD_I(glyphCentring) LOAD_I(fontChoice)
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
		ImGui::Text("Technique after \"The Betta Fish in ASCII\" by @dwcminh.ascii");
		ImGui::Text("(github.com/dwcminh/ascii-art, BETTA-FISH); our own GLSL.");
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
		ImGui::SliderInt("Font size (px, 0 = auto 1.1 x cell height)", &p.fontPx, 0, 24);
		static const char * centring[] = { "Line box (reference)", "Ink box (fit inside the cell)" };
		ImGui::Combo("Glyph centring", &p.glyphCentring, centring, 2);
		static const char * fonts[] = { "Consolas / Menlo if installed, else bundled (reference)", "Bundled JetBrains Mono" };
		ImGui::Combo("Font", &p.fontChoice, fonts, 2);
		if (atlasState == 1)
			ImGui::Text("Atlas: %d px font, %s", fontPxFor(cellH()), atlasFont.c_str());
		ImGui::SliderFloat("Black threshold", &p.blackThreshold, 0.0f, 0.3f);
		ImGui::SliderFloat("Minimum coverage", &p.minCoverage, 0.0f, 1.0f);
		ImGui::Text("(0, the reference: any covered pixel draws the cell; empty cells are exact black)");
		ImGui::SliderFloat("Outline brightness floor", &p.edgeFloor, 0.0f, 1.0f);
		ImGui::Checkbox("Outline part boundaries on one side only", &p.edgeOneSided);
		ImGui::Text("(on, the reference: the nearer side, by the parts' depth rank; seams once)");
		ImGui::Checkbox("Edges started by side neighbours only", &p.edgeSideOnly);
		ImGui::SliderFloat("Fill brightness floor", &p.fillFloor, 0.0f, 1.0f);
		ImGui::SliderFloat("Fill pool start (brightness)", &p.fillStart, 0.0f, 1.0f);
		ImGui::SliderFloat("Fill pool scale", &p.fillScale, 0.5f, 10.0f);
		ImGui::Text("(pool = (brightness - start) x scale, 0..3: dark to bright letters)");
		ImGui::SliderFloat("Mark threshold", &p.markThreshold, 0.0f, 0.5f);

		ImGui::Separator();
		ImGui::Text("Fill shimmer (letter steps per second, all cells together; reference 4)");
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
