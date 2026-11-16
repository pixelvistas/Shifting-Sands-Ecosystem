#include "AsciiGlyphRenderer.h"

// Defaults (DEVLOG, 2026-11-16; hysteresis and smoothing checked headless
// by tools/ascii_preview, not yet on the sandbox).
std::string AsciiGlyphRenderer::RAMP = " .:-=+*#";
float AsciiGlyphRenderer::SMOOTHING_SEC = 0.5f;
float AsciiGlyphRenderer::HYSTERESIS_STEPS = 0.15f;
bool AsciiGlyphRenderer::DITHER = true;
float AsciiGlyphRenderer::BRIGHTNESS = 1.0f;

// Height-band palettes, low .. high (DEVLOG, 2026-11-16; checked with
// tools/ascii_preview palette-check: adjacent bands at least 10 dE00 apart
// (glyphs) and 5 dE00 (backgrounds) under simulated protanopia,
// deuteranopia and tritanopia, glyph/background contrast >= 4:1). No green
// or cyan hues at all: no saturated red next to saturated green, and no
// band reads as water or vegetation.
static ofFloatColor hexColor(unsigned int h)
{
	return ofFloatColor(((h >> 16) & 0xff) / 255.0f, ((h >> 8) & 0xff) / 255.0f, (h & 0xff) / 255.0f);
}
const ofFloatColor AsciiGlyphRenderer::DEFAULT_FG_COLOR[BANDS] = {
	hexColor(0x776CC1),   // low: slate violet
	hexColor(0xB684C4),   // low-mid: lilac
	hexColor(0xF191AF),   // mid: rose
	hexColor(0xF0C66D),   // high-mid: amber
	hexColor(0xF6F1DA) }; // high: warm white
const ofFloatColor AsciiGlyphRenderer::DEFAULT_BG_COLOR[BANDS] = {
	hexColor(0x161328),
	hexColor(0x261C28),
	hexColor(0x3D242B),
	hexColor(0x3E3628),
	hexColor(0x46443D) };
ofFloatColor AsciiGlyphRenderer::FG_COLOR[BANDS] = {
	DEFAULT_FG_COLOR[0], DEFAULT_FG_COLOR[1], DEFAULT_FG_COLOR[2], DEFAULT_FG_COLOR[3], DEFAULT_FG_COLOR[4] };
ofFloatColor AsciiGlyphRenderer::BG_COLOR[BANDS] = {
	DEFAULT_BG_COLOR[0], DEFAULT_BG_COLOR[1], DEFAULT_BG_COLOR[2], DEFAULT_BG_COLOR[3], DEFAULT_BG_COLOR[4] };
bool AsciiGlyphRenderer::SHOW_CONTOURS = true;
float AsciiGlyphRenderer::CONTOUR_INTERVAL = 10.0f;
float AsciiGlyphRenderer::CONTOUR_OFFSET = 5.0f; // contours at 5, 15, 25 mm: flat sand (~9 mm) sits between two
float AsciiGlyphRenderer::CONTOUR_BRIGHTEN = 0.6f;

static ofMesh makeQuad(float w, float h)
{
	ofMesh q;
	q.setMode(OF_PRIMITIVE_TRIANGLE_STRIP);
	q.addVertex(glm::vec3(0, 0, 0)); q.addTexCoord(glm::vec2(0, 0));
	q.addVertex(glm::vec3(w, 0, 0)); q.addTexCoord(glm::vec2(w, 0));
	q.addVertex(glm::vec3(0, h, 0)); q.addTexCoord(glm::vec2(0, h));
	q.addVertex(glm::vec3(w, h, 0)); q.addTexCoord(glm::vec2(w, h));
	return q;
}

static void allocateFloatFbo(ofFbo & fbo, int w, int h)
{
	ofFboSettings s;
	s.width = w;
	s.height = h;
	s.internalformat = GL_RGBA32F;
	s.textureTarget = GL_TEXTURE_RECTANGLE_ARB;
	s.minFilter = GL_NEAREST;
	s.maxFilter = GL_NEAREST;
	s.wrapModeHorizontal = GL_CLAMP_TO_EDGE;
	s.wrapModeVertical = GL_CLAMP_TO_EDGE;
	s.useDepth = false;
	fbo.allocate(s);
	fbo.begin();
	ofClear(0, 0, 0, 0);
	fbo.end();
}

bool AsciiGlyphRenderer::setup()
{
#ifdef TARGET_OPENGLES
	shadersLoaded = false;
#else
	const std::string dir = ofIsGLProgrammableRenderer() ? "shaders/ascii/GL3/" : "shaders/ascii/GL2/";
	shadersLoaded = sampleShader.load(dir + "ascii.vert", dir + "asciiSample.frag")
		&& stateShader.load(dir + "ascii.vert", dir + "asciiState.frag")
		&& glyphShader.load(dir + "ascii.vert", dir + "asciiGlyph.frag");
#endif
	if (!shadersLoaded)
		ofLogError("AsciiGlyphRenderer") << "setup(): could not load shaders/ascii";
	return shadersLoaded;
}

void AsciiGlyphRenderer::setProjectorRes(const ofVec2f & res)
{
	projRes = res;
	projQuad = makeQuad(projRes.x, projRes.y);
	cols = rows = 0; // reallocate the cell FBOs
}

int AsciiGlyphRenderer::getLevels() const
{
	return std::max(2, std::min((int)MAX_RAMP, (int)RAMP.size())); // (int): no ODR-use of MAX_RAMP
}

void AsciiGlyphRenderer::setCellHeightPx(int px)
{
	cellHeightPx = std::max(4, px);
	if (atlas.build(cellHeightPx))
		allocateCells();
}

void AsciiGlyphRenderer::allocateCells()
{
	const int cw = atlas.getCellWidth(), ch = atlas.getCellHeight();
	if (cw <= 0 || ch <= 0 || projRes.x < 1 || projRes.y < 1)
		return;
	const int c = (int)std::ceil(projRes.x / cw), r = (int)std::ceil(projRes.y / ch);
	if (c == cols && r == rows && sampleFbo.isAllocated())
		return;
	cols = c;
	rows = r;
	allocateFloatFbo(sampleFbo, cols, rows);
	allocateFloatFbo(stateFbo[0], cols, rows);
	allocateFloatFbo(stateFbo[1], cols, rows);
	cellQuad = makeQuad((float)cols, (float)rows);
	resetPending = true;
	if (!outFbo.isAllocated() || (int)outFbo.getWidth() != (int)projRes.x || (int)outFbo.getHeight() != (int)projRes.y) {
		outFbo.allocate((int)projRes.x, (int)projRes.y, GL_RGBA);
		outFbo.getTexture().setTextureMinMagFilter(GL_NEAREST, GL_NEAREST);
	}
}

void AsciiGlyphRenderer::update(ofTexture & field, ofVboMesh & mesh, float dt)
{
	if (!isReady() || cols <= 0 || !field.isAllocated())
		return;
	const int levels = getLevels();
	CONTOUR_INTERVAL = std::max(0.5f, CONTOUR_INTERVAL);
	if ((int)lastRamp.size() != (int)RAMP.size() || lastDither != DITHER
		|| lastContourInterval != CONTOUR_INTERVAL || lastContourOffset != CONTOUR_OFFSET)
		resetPending = true; // the held levels mean something else now
	lastRamp = RAMP;
	lastDither = DITHER;
	lastContourInterval = CONTOUR_INTERVAL;
	lastContourOffset = CONTOUR_OFFSET;
	const float alpha = SMOOTHING_SEC > 0.0f ? 1.0f - std::exp(-std::max(0.0f, dt) / SMOOTHING_SEC) : 1.0f;
	const float cw = (float)atlas.getCellWidth(), ch = (float)atlas.getCellHeight();

	ofPushStyle();
	ofEnableBlendMode(OF_BLENDMODE_DISABLED);
	ofSetColor(255);

	// 1. Sample the field once per cell.
	sampleFbo.begin();
	ofClear(0, 0, 0, 0);
	sampleShader.begin();
	sampleShader.setUniformTexture("fieldTex", field, 0);
	ofPushMatrix();
	ofScale(1.0f / cw, 1.0f / ch);
	mesh.draw();
	ofPopMatrix();
	sampleShader.end();
	sampleFbo.end();

	// 2. Smooth and hold (ping-pong).
	const int next = 1 - current;
	stateFbo[next].begin();
	ofClear(0, 0, 0, 0);
	stateShader.begin();
	stateShader.setUniformTexture("sampleTex", sampleFbo.getTexture(), 0);
	stateShader.setUniformTexture("prevTex", stateFbo[current].getTexture(), 1);
	stateShader.setUniform1f("alpha", alpha);
	stateShader.setUniform1f("levels", (float)levels);
	stateShader.setUniform1f("hysteresis", std::max(0.0f, HYSTERESIS_STEPS));
	stateShader.setUniform1i("dither", DITHER ? 1 : 0);
	stateShader.setUniform1i("resetAll", resetPending ? 1 : 0);
	stateShader.setUniform1f("bands", (float)BANDS);
	stateShader.setUniform1f("fieldLo", fieldLo);
	stateShader.setUniform1f("fieldSpan", fieldSpan);
	stateShader.setUniform1f("contourInterval", CONTOUR_INTERVAL);
	stateShader.setUniform1f("contourOffset", CONTOUR_OFFSET);
	cellQuad.draw();
	stateShader.end();
	stateFbo[next].end();
	current = next;
	resetPending = false;

	// 3. Glyphs at projector resolution.
	float ramp[MAX_RAMP];
	for (int i = 0; i < MAX_RAMP; i++)
		ramp[i] = (float)AsciiFontAtlas::indexOf(i < (int)RAMP.size() ? RAMP[i] : ' ');
	outFbo.begin();
	ofClear(0, 0, 0, 255);
	glyphShader.begin();
	glyphShader.setUniformTexture("stateTex", stateFbo[current].getTexture(), 0);
	glyphShader.setUniformTexture("atlasTex", atlas.getTexture(), 1);
	glyphShader.setUniform2f("cellSize", cw, ch);
	glyphShader.setUniform1fv("rampGlyph", ramp, MAX_RAMP);
	glyphShader.setUniform1f("brightness", ofClamp(BRIGHTNESS, 0.0f, 1.0f));
	float fg[3 * BANDS], bg[3 * BANDS];
	for (int b = 0; b < BANDS; b++) {
		fg[3 * b + 0] = FG_COLOR[b].r; fg[3 * b + 1] = FG_COLOR[b].g; fg[3 * b + 2] = FG_COLOR[b].b;
		bg[3 * b + 0] = BG_COLOR[b].r; bg[3 * b + 1] = BG_COLOR[b].g; bg[3 * b + 2] = BG_COLOR[b].b;
	}
	glyphShader.setUniform3fv("fgColor", fg, BANDS);
	glyphShader.setUniform3fv("bgColor", bg, BANDS);
	glyphShader.setUniform1i("showContours", SHOW_CONTOURS ? 1 : 0);
	glyphShader.setUniform1f("contourBrighten", ofClamp(CONTOUR_BRIGHTEN, 0.0f, 1.0f));
	projQuad.draw();
	glyphShader.end();
	outFbo.end();

	ofPopStyle();
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
	hasFrame = true;
}

void AsciiGlyphRenderer::draw(float x, float y, float w, float h)
{
	ofPushStyle();
	ofEnableBlendMode(OF_BLENDMODE_DISABLED);
	if (hasFrame && outFbo.isAllocated()) {
		ofSetColor(255);
		outFbo.draw(x, y, w, h);
	} else {
		ofSetColor(0);
		ofDrawRectangle(x, y, w, h);
	}
	ofPopStyle();
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
}
