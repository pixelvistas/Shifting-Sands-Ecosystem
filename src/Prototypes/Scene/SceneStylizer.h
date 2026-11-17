/***********************************************************************
SceneStylizer - Prototype 4 ("Scene"), pass 3: the screen-space stylise
filter, in projector pixels, after the scene has been warped onto the
projector (so glyphs stay upright and dots stay pixel-crisp).

STYLE: Off (the plain warped scene), ASCII, Dither, Both.
- ASCII, shape-aware: cells CELL_MM high on the sand (whole projector
  pixels; width from the font), the cell's mean colour and luminance
  (sceneCells.frag) and the structure tensor of its luminance gradient
  (sceneEdges.frag: edge strength and stroke angle in screen space, after
  the warp). Above EDGE_THRESHOLD, a directional glyph by the stroke angle
  quantised to four directions (DIR_GLYPHS: light/heavy for horizontal,
  "\\", vertical, "/"; heavy above WEIGHT_SPLIT); otherwise a glyph from
  RAMP by luminance (black point, LUMINANCE_GAMMA). DEBUG_EDGES colours
  edge cells by direction. The
  ramp is sorted at runtime by each glyph's measured ink coverage in the
  atlas (AsciiFontAtlas from Prototype 3, reused unchanged), so the
  editable ramp only says which characters to use. Glyph coverage x the
  cell's scene colour x COLOUR_BOOST; dark cells are empty.
- Dither: ordered dither on DOT_SIZE-pixel dots, LEVELS per channel, a
  4 x 4 or 8 x 8 Bayer threshold fixed in projector space (or LINE_SCREEN:
  horizontal 1-pixel lines, period LINE_PERIOD). PALETTE_ON (default):
  the dot's luminance mapped to 2 or 3 stops first (presets Gold, Night,
  Green: default Green); off: per channel. Black stays black.
- Both: the dither, with glyphs over it in cells brighter than
  BOTH_THRESHOLD.
A static scene gives a static output: nothing here depends on time.
***********************************************************************/

#pragma once
#include "ofMain.h"
#include "../Ascii/AsciiFontAtlas.h"

class SceneStylizer
{
public:
	enum { STYLE_OFF = 0, STYLE_ASCII = 1, STYLE_DITHER = 2, STYLE_BOTH = 3 };
	enum { PALETTE_GOLD = 0, PALETTE_NIGHT = 1, PALETTE_GREEN = 2 };
	static const int MAX_RAMP = 32;

	bool setup();
	void setProjectorRes(const ofVec2f & res);
	void setCellHeightPx(int px);   // rebuilds the atlas and the sorted ramp when needed
	// warped: projector-sized scene; result in getOutput().
	void render(ofTexture & warped, float blackPoint);
	ofFbo & getOutput() { return outFbo; }
	int getCellWidth() const { return atlas.getCellWidth(); }
	int getCellHeight() const { return atlas.getCellHeight(); }
	int getFontSize() const { return atlas.getFontSize(); }
	const std::string & getSortedRamp() const { return sortedRamp; }
	static void applyPalettePreset(int preset);

	static int STYLE;
	static float CELL_MM;
	static float LUMINANCE_GAMMA;
	static float COLOUR_BOOST;
	static std::string RAMP;             // leading space; sorted by coverage at runtime
	static int DOT_SIZE;                 // projector pixels
	static int LEVELS;                   // per channel
	static bool BAYER8;
	static bool PALETTE_ON;
	static int PALETTE_PRESET;
	static int PALETTE_STOPS;            // 2 or 3
	static ofFloatColor PALETTE[3];
	static float BOTH_THRESHOLD;
	static float EDGE_THRESHOLD;         // luminance per pixel (coherent gradient)
	static std::string DIR_GLYPHS;       // 8: light/heavy for "-", "\\", "|", "/"
	static float WEIGHT_SPLIT;
	static bool DEBUG_EDGES;
	static bool LINE_SCREEN;
	static int LINE_PERIOD;              // projector pixels

private:
	void sortRamp();
	AsciiFontAtlas atlas;
	ofShader cellShader, edgeShader, stylizeShader;
	bool loaded = false;
	ofVec2f projRes;
	ofFbo cellFbo, edgeFbo, outFbo;
	int cols = 0, rows = 0;
	ofMesh cellQuad, projQuad;
	std::string sortedRamp, sortedFrom;
	int sortedForCell = 0;
	std::vector<float> coverage;         // per atlas glyph (printable ASCII)
};
