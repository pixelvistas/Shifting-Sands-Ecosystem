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
  2. asciiState (ping-pong): per cell, an exponential moving average of
     the sample (time constant SMOOTHING_SEC) and the glyph level held
     with hysteresis: x = value x (levels - 1) plus the cell's 4 x 4
     Bayer threshold d (0.5 when dithering is off); the held level g
     changes only when x + d leaves [g - HYSTERESIS_STEPS,
     g + 1 + HYSTERESIS_STEPS), and then to floor(x + d).
  3. asciiGlyph: at projector resolution, each pixel looks up its cell's
     level, the ramp character for it, and the matching atlas texel
     (one texel per pixel: cells are whole projector pixels, the grid
     starts at projector pixel 0, 0); coverage >= 0.5 is lit, the rest
     black: crisp monochrome characters.

tools/ascii_preview/ runs these same GL3 shader files headless.
***********************************************************************/

#pragma once
#include "ofMain.h"
#include "AsciiFontAtlas.h"

class AsciiGlyphRenderer
{
public:
	static const int MAX_RAMP = 16;

	// Loads the shaders (GL work: call only once the module is shown).
	bool setup();
	void setProjectorRes(const ofVec2f & res);
	// Cell height in projector pixels; the width follows the font.
	void setCellHeightPx(int px);
	// Forgets the smoothed values and held glyphs (next update starts fresh).
	void reset() { resetPending = true; }
	// field: R = value 0..1 (< 0: no data); mesh: projector px / field texels.
	void update(ofTexture & field, ofVboMesh & mesh, float dt);
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
	static float BRIGHTNESS;          // 0..1, grey level of lit pixels

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
	ofMesh cellQuad, projQuad;
	bool hasFrame = false;
};
