/***********************************************************************
AsciiFilter - a screen-space ASCII filter: takes textures (a colour
picture, a parts buffer and optional mark layers, all rectangle textures
the size of the output) and draws them as a dense grid of coloured
characters on pure black. Knows nothing about the mode that produced the
pictures; the caller says what the part fields and mark layers are.

Visual reference: "The Betta Fish in ASCII" by @dwcminh.ascii (dense
true-colour characters on black, stroke characters along edges, letters
by brightness, double glow, short trails). Its code is licensed for
personal, non-commercial use. This filter was written from a description
of the technique (the project brief) before that code was read; the code
was read afterwards, on 2026-11-21, only to compare (DEVLOG), and nothing
was ported or copied from it. The brief's constants (fill pools, 7 x 10
cells, outline rule, edge floor 0.85, coverage curve, trail 0.5, glow
0.75 / 0.45) match that code's.

Inputs (per frame):
- colour: any RGB picture; black (at or under blackThreshold) is "nothing".
- parts: per pixel R, G, B = three part fields (8-bit values), A = 1 where
  there is a part. Cells whose parts differ get outlines; G also selects
  the shimmer class (0, 1, 2) of the fill letters. Sandbox: R = elevation
  band, G = layer (0 land, 1 plants, 2 water), B = succession stage.
- marks: beginMarks(), then addMarks(texture, glyph, premultiplied) per
  layer (later layers replace earlier ones where they show). A cell with a
  mark draws that layer's glyph in its colour.

Passes (shaders in bin/data/shaders/ascii/GL2|GL3, see each file):
1. asciiCells: per cell (cell preset, e.g. 7 x 10 px), 4 x 4 samples:
   coverage = covered fraction, colour = mean of covered samples after the
   pre-filter (exposure, saturation).
2. asciiParts: the dominant part of the cell (centre first, mode).
3. asciiMarks: the brightest mark pixel in the cell.
4. asciiDecide: mark glyph, or empty (coverage < minCoverage), or an
   outline stroke (| - _ / \ +) from the directions to empty or
   different-part neighbours (brightness floor edgeFloor), or a fill
   letter from 4 luminance pools with a slow per-cell shimmer; brightness
   x (0.30 + 0.70 x coverage^0.7).
5. asciiGlyphs: the character from the font atlas x the cell colour.
6. asciiTrail: max(previous x trailAmount, this frame) (feedback FBOs).
7. Glow: 1/4-resolution blur (tight) and a 1/8 blur made from it (wide),
   added at tightAmount / wideAmount through a black floor; masked to
   drawn cells unless glowOutside, so empty cells stay exact black.

Font atlas: ASCII 32..126 baked at start-up (and on a cell-preset change)
at the cell size from fonts/JetBrainsMono-Regular.ttf (SIL OFL 1.1,
fonts/OFL.txt): the font size is chosen so the advance fits the cell width
and the ink of all 95 characters fits the cell height.

All GL objects are made lazily on first use, so use one instance from one
GL context (one window). Not compiled with openFrameworks here, not
hardware-tested.
***********************************************************************/

#pragma once
#include "ofMain.h"

class AsciiFilter
{
public:
	enum { CELLS_6x10 = 0, CELLS_7x10 = 1, CELLS_8x12 = 2, CELLS_10x16 = 3, CELL_PRESETS = 4 };
	static const int CELL_W[CELL_PRESETS];
	static const int CELL_H[CELL_PRESETS];

	struct Params {
		bool enabled;
		int cellPreset;         // CELLS_*
		float blackThreshold;   // max channel: a sample at or under this is black
		float minCoverage;      // under this a cell is empty (exact black)
		float edgeFloor;        // outline brightness floor (max channel)
		bool edgeOneSided;      // part boundaries drawn by one side only
		float shimmerRate[3];   // fill-letter steps per second, by shimmer class (parts G)
		float trailAmount;      // 0 = no trails
		float glowTight;        // amount of the 1/4-resolution glow
		float glowWide;         // amount of the 1/8-resolution glow
		float glowFloor;        // glow at or under this is 0
		bool glowOutside;       // let the glow light empty cells
		float markThreshold;    // mark layers: brightest channel above this is a mark
		float exposure;         // pre-filter, x colour
		float saturation;       // pre-filter, 0 grey, 1 unchanged
	};
	static const Params DEFAULTS;   // defined at the top of AsciiFilter.cpp
	Params params = DEFAULTS;

	// Marks for this frame: beginMarks(w, h) clears them (w, h: the output
	// size), addMarks() draws one layer; draw() uses them once.
	void beginMarks(int w, int h);
	void addMarks(const ofTexture & layer, char glyph, bool premultiplied);
	// colour and parts: GL_TEXTURE_RECTANGLE textures of the same size.
	// Returns false (and draws nothing) if the filter cannot draw.
	bool draw(const ofTexture & colour, const ofTexture & parts, float x, float y);
	bool isReady();

	// Widgets only, under a collapsing header, into the current ImGui window.
	void drawGui();
	void setPresetFile(const std::string & path) { presetFile = path; }
	// GUI labels of the shimmer classes 0..2 (parts G).
	void setClassNames(const std::string & a, const std::string & b, const std::string & c) { classNames[0] = a; classNames[1] = b; classNames[2] = c; }
	// False once the shaders or the font failed to load (the caller then
	// draws unfiltered).
	bool shadersFailed() const { return loadState == -1 || atlasState == -1; }
	bool savePreset();
	bool loadPreset();      // reads only the entries present

	int cellW() const { return CELL_W[clampPreset(params.cellPreset)]; }
	int cellH() const { return CELL_H[clampPreset(params.cellPreset)]; }
	float gpuMs() const { return timer.ms(); }

	// Fill pools, dark to bright (the defaults; at most 48 characters).
	static const char * POOLS[4];
	static const char * FONT_FILE;

private:
	struct GpuTimer {
		void begin();
		void end();
		float ms() const { return lastMs; }
	private:
		unsigned int ids[2] = { 0, 0 };
		bool pending[2] = { false, false };
		bool started = false;
		int cur = 0;
		bool supported = true, checked = false;
		float lastMs = 0.0f;
	};

	static int clampPreset(int p) { return std::max(0, std::min(CELL_PRESETS - 1, p)); }
	bool loadShaders();
	bool ensureAtlas(int cw, int ch);
	void ensureTargets(int w, int h, int cw, int ch);

	ofShader cellsShader, partsShader, marksShader, markWriteShader, decideShader,
		glyphsShader, trailShader, downShader, blurShader, outShader;
	int loadState = 0;      // 0 not tried, 1 loaded, -1 failed
	int atlasState = 0;     // 0 not made, 1 made, -1 font failed
	int atlasCw = 0, atlasCh = 0;
	ofFbo atlasFbo;
	ofFbo cellFbo, partFbo, markCellFbo, decideFbo;   // one texel per cell
	ofFbo marksFbo, frameFbo, trailFbo[2];            // output size
	ofFbo tightFbo[2], wideFbo[2];                    // 1/4 and 1/8 size
	int outW = 0, outH = 0, gridW = 0, gridH = 0;
	int trailCur = 0;
	bool trailValid = false;
	bool marksThisFrame = false;
	std::string classNames[3] = { "Class 0", "Class 1", "Class 2" };
	GpuTimer timer;
	bool timing = false;
	std::string presetFile = "settings/asciiFilter.xml";
	std::string presetStatus;
};
