#include "AsciiGlyphRenderer.h"

// Defaults (DEVLOG, 2026-11-16; hysteresis and smoothing checked headless
// by tools/ascii_preview, not yet on the sandbox).
std::string AsciiGlyphRenderer::RAMP = " .:-=+*#";
float AsciiGlyphRenderer::SMOOTHING_SEC = 0.5f;
float AsciiGlyphRenderer::HYSTERESIS_STEPS = 0.15f;
bool AsciiGlyphRenderer::DITHER = true;
float AsciiGlyphRenderer::BRIGHTNESS = 1.0f;

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
	if ((int)lastRamp.size() != (int)RAMP.size() || lastDither != DITHER)
		resetPending = true; // the held levels mean something else now
	lastRamp = RAMP;
	lastDither = DITHER;
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
