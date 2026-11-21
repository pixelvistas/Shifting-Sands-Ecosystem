/***********************************************************************
AsciiFilter - a screen-space ASCII filter: takes textures (a colour
picture, a parts buffer and optional mark layers, all rectangle textures
the size of the output) and draws them as a dense grid of coloured
characters on pure black. Knows nothing about the mode that produced the
pictures; the caller says what the part fields and mark layers are.

Reference: "The Betta Fish in ASCII" by @dwcminh.ascii,
https://github.com/dwcminh/ascii-art (folder BETTA-FISH, betta_fish.py; free
for personal, non-commercial use, never to be passed off as one's own).
The filter was first written from a description of the technique; on
2026-11-21 it was brought in line with how that code produces its look
(DEVLOG, same date: the comparison table, and what cannot match from 2D
inputs). The techniques are re-expressed here in our own GLSL and C++;
no code or passages were copied from the Python file.

Inputs (per frame):
- colour: any RGB picture; black (at or under blackThreshold) is "nothing".
- parts: per pixel R, G, B = three part fields (8-bit values), A = 1 where
  there is a part. R is the depth rank (higher = nearer the viewer), G and
  B the part's identity; G also selects the shimmer class (0, 1, 2).
  Sandbox: R = elevation band (seen from above, higher sand is nearer),
  G = layer (0 land, 1 plants, 2 water), B = succession stage.
- marks: beginMarks(), then addMarks(texture, glyph, premultiplied) per
  layer (later layers replace earlier ones where they show). A cell with a
  mark draws that layer's glyph in its colour.

Passes (shaders in bin/data/shaders/ascii/GL2|GL3, see each file):
1. asciiParts: the part of each cell (the most common of its centre and a
   4 x 4 grid of points; the reference keeps its nearest point's part).
2. asciiCells: coverage = covered pixels / pixels (every pixel read);
   colour = mean of the covered pixels of the cell's part, pre-filtered.
3. asciiMarks: the brightest mark pixel in the cell.
4. asciiDecide, the reference's rules: L = brightest channel. Mark glyph;
   empty (no coverage); outline (a side neighbour that is empty, lower in
   depth, or across a seam; direction from all 8; | - _ / \ + at 2:1),
   brightness max(0.85, L); fill letter from 4 pools by
   (L - 0.42) x 4.6, entry (h + floor(4t + h mod 11)) mod 12 with an
   integer cell hash, brightness 0.45 + 0.55 L / 0.9; x (0.30 + 0.70
   coverage^0.7); colour rgb x min(brightness, 1.4), clipped.
5. asciiGlyphs: the character from the font atlas x the cell colour.
6. asciiTrail: max(previous x trailAmount, this frame) (feedback FBOs).
7. Glow: 1/4 box shrink + Gaussian sigma 1.78, 1/8 from it + sigma 2.40
   (pygame-ce gaussian_blur radius 2 and 3, measured), added at 0.75 /
   0.45. Masked to drawn cells unless glowOutside, so empty cells stay
   exact black (the reference has no mask; see asciiOut.frag).

Font atlas: ASCII 32..126, one cell per character, baked on first use and
whenever the cell preset, font size, centring or font choice changes.
Default (as the reference): font size 1.1 x the cell height (11 px in a
10 px cell), each character centred by its advance and line box, overflow
cropped. Font: as the reference's list, Consolas (Windows) or Menlo (macOS)
if installed, else the bundled fonts/JetBrainsMono-Regular.ttf (SIL OFL
1.1, fonts/OFL.txt); or always the bundled one.

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
	enum { GLYPH_CENTRE_LINE_BOX = 0, GLYPH_CENTRE_INK_BOX = 1 };
	enum { FONT_REFERENCE_ORDER = 0, FONT_BUNDLED = 1 };
	static const int CELL_W[CELL_PRESETS];
	static const int CELL_H[CELL_PRESETS];

	struct Params {
		bool enabled;
		int cellPreset;         // CELLS_*
		float blackThreshold;   // max channel: a sample at or under this is black
		float minCoverage;      // under this a cell is empty (exact black)
		float edgeFloor;        // outline brightness floor (max channel)
		bool edgeOneSided;      // reference rule: the nearer side (parts R), seams from one side; off: both sides
		bool edgeSideOnly;      // reference: only the 4 side neighbours start an edge (all 8 give the direction)
		float fillFloor;        // fill brightness floor (reference 0.45)
		float fillStart;        // fill pool level = (L - fillStart) x fillScale (reference 0.42, 4.6)
		float fillScale;
		int fontPx;             // 0 = auto: 1.1 x cell height (reference: 11 px in 10 px cells)
		int glyphCentring;      // GLYPH_CENTRE_*
		int fontChoice;         // FONT_*
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

	// Fill pools, dark to bright; each is repeated to 12 entries.
	static const char * POOLS[4];
	static const char * FONT_FILE;      // bundled
	int fontPxFor(int ch) const { return params.fontPx > 0 ? params.fontPx : (int)std::floor(1.1f * ch + 0.5f); }
	const std::string & atlasFontName() const { return atlasFont; }

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
	bool ensureAtlas(int cw, int ch, int px, int centring, int fontChoice);
	void ensureTargets(int w, int h, int cw, int ch);

	ofShader cellsShader, partsShader, marksShader, markWriteShader, decideShader,
		glyphsShader, trailShader, downShader, blurShader, outShader;
	int loadState = 0;      // 0 not tried, 1 loaded, -1 failed
	int atlasState = 0;     // 0 not made, 1 made, -1 font failed
	int atlasCw = 0, atlasCh = 0, atlasPx = 0, atlasCentring = -1, atlasFontChoice = -1;
	std::string atlasFont;
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
