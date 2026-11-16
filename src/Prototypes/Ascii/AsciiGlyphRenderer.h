/***********************************************************************
AsciiGlyphRenderer - Prototype 3 ("ASCII"): turns a scalar field into a
grid of monospace characters in projector space.

Input: any scalar field as a rectangle texture (R: value 0..1, negative =
no data) plus a mesh that places it on the projector (vertices in
projector pixels, texture coordinates in field texels). Three GLSL
passes (bin/data/shaders/ascii/GL2|GL3, shared vertex shader ascii.vert):

  1. asciiSample: the mesh is drawn into a float FBO with one texel per
     glyph cell; each texel averages a 3 x 3 footprint of the field
     around the cell centre (footprint from the screen-space
     derivatives).
  2. asciiState (ping-pong): per cell, an exponential moving average v of
     the sample (time constant SMOOTHING_SEC), and three levels held with
     hysteresis, all from v:
       glyph level: x = v x (levels - 1) + d, d = the cell's 4 x 4 Bayer
         threshold (0.5 when dithering is off); the held level g changes
         only when x leaves [g - HYSTERESIS_STEPS, g + 1 + HYSTERESIS_STEPS),
         and then to floor(x);
       band (BANDS = 5, low .. high): the same rule on v x (BANDS - 1) + d
         (same Bayer threshold, so bands dither between neighbours);
       contour index: floor((value in field units - CONTOUR_OFFSET) /
         CONTOUR_INTERVAL), held by the same rule without dithering.
  3. asciiGlyph: at projector resolution, each pixel looks up its cell's
     glyph level, the ramp character for it, and the matching atlas texel
     (one texel per pixel: cells are whole projector pixels, the grid
     starts at projector pixel 0, 0); coverage >= 0.5 is the glyph
     (alpha 1), the rest alpha 0, drawn as mix(BG_COLOR[band],
     FG_COLOR[band], alpha) x BRIGHTNESS. A cell whose held contour index
     is above a 4-neighbour's is on a contour: its glyph colour is mixed
     towards white by CONTOUR_BRIGHTEN. Every decision comes from held
     levels, so nothing flickers that the glyphs do not.
Bands are height bands only; nothing here knows about water or plants.

tools/ascii_preview/ runs these same GL3 shader files headless.
***********************************************************************/

#pragma once
#include "ofMain.h"
#include "AsciiFontAtlas.h"

class AsciiGlyphRenderer
{
public:
	static const int MAX_RAMP = 16;
	static const int BANDS = 5;

	// Loads the shaders (GL work: call only once the module is shown).
	bool setup();
	void setProjectorRes(const ofVec2f & res);
	// Cell height in projector pixels; the width follows the font.
	void setCellHeightPx(int px);
	// Forgets the smoothed values and held glyphs (next update starts fresh).
	void reset() { resetPending = true; }
	// field: R = value 0..1 (< 0: no data); mesh: projector px / field texels.
	void update(ofTexture & field, ofVboMesh & mesh, float dt);
	// Field value 0..1 -> field units (mm for elevation): lo + value x span.
	// Used for the contour interval only.
	void setFieldUnits(float lo, float span) { fieldLo = lo; fieldSpan = span; }
	void draw(float x, float y, float w, float h);

	int getCols() const { return cols; }
	int getRows() const { return rows; }
	int getCellWidth() const { return atlas.getCellWidth(); }
	int getCellHeight() const { return atlas.getCellHeight(); }
	int getFontSize() const { return atlas.getFontSize(); }
	int getLevels() const;
	bool isReady() const { return shadersLoaded && atlas.isReady(); }

	// Live settings (ASCII panel, preset).
	static std::string RAMP;          // darkest to densest, 2..16 characters
	static float SMOOTHING_SEC;       // EMA time constant (0 = none)
	static float HYSTERESIS_STEPS;    // in ramp steps
	static bool DITHER;
	static float BRIGHTNESS;          // 0..1, multiplies the cell colour
	static ofFloatColor FG_COLOR[BANDS];   // glyph colour per band, low .. high
	static ofFloatColor BG_COLOR[BANDS];   // muted cell background per band
	static const ofFloatColor DEFAULT_FG_COLOR[BANDS];
	static const ofFloatColor DEFAULT_BG_COLOR[BANDS];
	static bool SHOW_CONTOURS;
	static float CONTOUR_INTERVAL;    // field units (mm)
	static float CONTOUR_OFFSET;      // field units (mm): contours at offset + k x interval
	static float CONTOUR_BRIGHTEN;    // 0..1, glyph colour mixed towards white

private:
	void allocateCells();
	ofVec2f projRes;
	int cellHeightPx = 0;
	int cols = 0, rows = 0;
	AsciiFontAtlas atlas;
	ofShader sampleShader, stateShader, glyphShader;
	bool shadersLoaded = false;
	ofFbo sampleFbo;
	ofFbo stateFbo[2];
	ofFbo outFbo;    // projector resolution, the composited output
	int current = 0;
	bool resetPending = true;
	std::string lastRamp;
	bool lastDither = true;
	float lastContourInterval = -1.0f, lastContourOffset = -1.0f;
	float fieldLo = 0.0f, fieldSpan = 1.0f;
	ofMesh cellQuad, projQuad;
	bool hasFrame = false;
};
