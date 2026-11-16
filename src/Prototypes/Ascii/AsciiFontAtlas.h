/***********************************************************************
AsciiFontAtlas - Prototype 3 ("ASCII"): the printable ASCII characters
(32..126) of a bundled monospace font rendered into one texture row,
one glyph cell per character, at the exact cell size the projection uses
(one atlas texel per projector pixel, so glyphs are never resampled).

Font: JetBrains Mono Bold, bin/data/ascii/fonts/JetBrainsMono-Bold.ttf,
SIL Open Font License 1.1 (bin/data/ascii/fonts/OFL.txt).

Sizing: the font is loaded at a reference size, the ink extent of all
printable ASCII glyphs (highest top to lowest bottom) is measured, and
the font is reloaded so that this extent fits the cell height; the cell
width is the font's advance at that size (monospace). The glyphs are
centred vertically. tools/ascii_preview/ascii_preview.py repeats this
sizing with Pillow (same FreeType rasteriser).

The atlas is white glyph coverage in R (0..1) on black; the shader
thresholds it at 0.5 for crisp monochrome characters.
***********************************************************************/

#pragma once
#include "ofMain.h"

class AsciiFontAtlas
{
public:
	static const int FIRST_CHAR = 32;
	static const int CHAR_COUNT = 95; // 32..126

	// (Re)builds the atlas for a cell cellHeightPx tall. Returns false if
	// the font could not be loaded. Cheap to call repeatedly: rebuilds
	// only when the height changes.
	bool build(int cellHeightPx);
	bool isReady() const { return ready; }
	int getCellWidth() const { return cellW; }
	int getCellHeight() const { return cellH; }
	int getFontSize() const { return fontSize; }
	ofTexture & getTexture() { return fbo.getTexture(); }
	// Atlas index of a character (non-printable characters map to ' ').
	static int indexOf(char c);

	static const char * FONT_FILE;

private:
	struct Metrics { float top = 0, bottom = 0, advance = 0; };
	static Metrics measure(ofTrueTypeFont & font);

	ofFbo fbo;
	bool ready = false;
	int cellW = 0, cellH = 0, fontSize = 0;
};
