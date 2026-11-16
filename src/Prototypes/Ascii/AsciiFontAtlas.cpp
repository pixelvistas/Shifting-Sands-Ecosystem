#include "AsciiFontAtlas.h"

const char * AsciiFontAtlas::FONT_FILE = "ascii/fonts/JetBrainsMono-Bold.ttf";

static const int REFERENCE_SIZE = 64; // px per em (loaded at 72 dpi: 1 pt = 1 px)
static const int DPI = 72;

int AsciiFontAtlas::indexOf(char c)
{
	const int i = (int)(unsigned char)c - FIRST_CHAR;
	return (i >= 0 && i < CHAR_COUNT) ? i : 0;
}

// Ink extent of the printable glyphs relative to the baseline (y down:
// top < 0) and the monospace advance (bounding box of ten 'M's minus that
// of one: nine advances, the ink overhangs cancel).
AsciiFontAtlas::Metrics AsciiFontAtlas::measure(ofTrueTypeFont & font)
{
	Metrics m;
	bool any = false;
	for (int c = FIRST_CHAR + 1; c < FIRST_CHAR + CHAR_COUNT; c++) {
		const ofRectangle r = font.getStringBoundingBox(std::string(1, (char)c), 0, 0);
		if (r.height <= 0.0f)
			continue;
		m.top = any ? std::min(m.top, r.y) : r.y;
		m.bottom = any ? std::max(m.bottom, r.y + r.height) : r.y + r.height;
		any = true;
	}
	m.advance = (font.stringWidth("MMMMMMMMMM") - font.stringWidth("M")) / 9.0f;
	return m;
}

bool AsciiFontAtlas::build(int cellHeightPx)
{
	cellHeightPx = std::max(4, cellHeightPx);
	if (ready && cellHeightPx == cellH)
		return true;
	ready = false;

	ofTrueTypeFont font;
	if (!font.load(FONT_FILE, REFERENCE_SIZE, true, false, false, 0.0f, DPI)) {
		ofLogError("AsciiFontAtlas") << "could not load " << FONT_FILE;
		return false;
	}
	const Metrics ref = measure(font);
	const float inkRef = std::max(1.0f, ref.bottom - ref.top);
	fontSize = std::max(3, (int)std::floor(REFERENCE_SIZE * cellHeightPx / inkRef));
	if (!font.load(FONT_FILE, fontSize, true, false, false, 0.0f, DPI)) {
		ofLogError("AsciiFontAtlas") << "could not load " << FONT_FILE << " at " << fontSize;
		return false;
	}
	const Metrics m = measure(font);
	cellH = cellHeightPx;
	cellW = std::max(2, (int)std::lround(m.advance));
	const float baseline = std::round(-m.top + 0.5f * (cellH - (m.bottom - m.top)));

	ofFboSettings s;
	s.width = cellW * CHAR_COUNT;
	s.height = cellH;
	s.internalformat = GL_RGBA;
	s.textureTarget = GL_TEXTURE_RECTANGLE_ARB; // the shaders read texel coordinates
	s.minFilter = GL_NEAREST;
	s.maxFilter = GL_NEAREST;
	s.wrapModeHorizontal = GL_CLAMP_TO_EDGE;
	s.wrapModeVertical = GL_CLAMP_TO_EDGE;
	s.useDepth = false;
	fbo.allocate(s);
	fbo.begin();
	ofClear(0, 0, 0, 0);
	ofPushStyle();
	ofEnableAlphaBlending();
	ofSetColor(255);
	for (int i = 1; i < CHAR_COUNT; i++)
		font.drawString(std::string(1, (char)(FIRST_CHAR + i)), (float)(i * cellW), baseline);
	ofPopStyle();
	fbo.end();
	ready = true;
	return true;
}
