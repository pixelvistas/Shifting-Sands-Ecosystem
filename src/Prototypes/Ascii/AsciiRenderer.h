/***********************************************************************
AsciiRenderer - Prototype 3 ("ASCII") display module.

Shows the sand as monospace characters on black: AsciiGlyphRenderer
(a generic scalar-field-to-glyph renderer, own shaders in
bin/data/shaders/ascii/GL2|GL3) fed with, for this first step, the
normalized elevation map.

- Input field: VegetationField's per-cell elevation (mm), normalized over
  the calibrated elevation range the sandbox uses, -/+ KinectProjector::
  getCalibratedCeilingElevation() (what ofApp gives VegetationField at
  start-up and what "Reset range to +-calibrated ceiling" restores; a
  range typed by hand into the Vegetation panel is not followed, since
  VegetationField does not expose it). 0 = calibrated floor, 1 = ceiling.
- Placement: the field is drawn through the projector calibration
  (KinectProjector::kinectCoordToProjCoord on a grid every LUT_STEP
  cells, as the seed and water-particle layers do).
- Glyph cells are sized in millimetres on the sand (GLYPH_MM, cell
  height): the projector's pixels per mm are measured from the
  calibration across the middle of the play area when the mode is first
  shown, when the grid changes, or on "Re-measure scale"; the cell height
  is rounded to whole projector pixels.

Read-only against KinectProjector, VegetationField and the succession
model: the water and succession simulations keep running whatever is
shown. ofApp updates the module only while "ASCII" is the display mode;
no GL work happens before that, so the Sandbox output is untouched.

Settings preset "Prototype 3: ASCII": bin/data/settings/asciiPreset.xml
(Save/Load in the ASCII panel; loaded at start-up when present).
Defaults live in the .cpp files.

Removing the module: delete src/Prototypes/Ascii, bin/data/shaders/ascii,
bin/data/ascii, tools/ascii_preview, the .vcxproj(.filters) entries and
the ASCII lines of the "Display mode" code in ofApp.
***********************************************************************/

#pragma once
#include "ofMain.h"
#include "AsciiGlyphRenderer.h"

class KinectProjector;
class VegetationField;

class AsciiRenderer
{
public:
	// No GL work here: only pointers and the preset file.
	void setup(std::shared_ptr<KinectProjector> const & kp, const VegetationField * vf);
	void setProjectorRes(const ofVec2f & res);
	// Call every frame while the ASCII mode is shown (starts fresh when
	// it was not called the frame before).
	void update();
	void drawMainWindow(float x, float y, float w, float h);
	void drawProjectorWindow();
	void drawGui();

	bool savePreset();
	bool loadPreset();

	static float GLYPH_MM; // glyph cell height on the sand

private:
	void measureScale();
	void uploadField();
	void rebuildMesh();

	std::shared_ptr<KinectProjector> kinectProjector;
	const VegetationField * vegetation = nullptr;
	AsciiGlyphRenderer glyphs;
	ofVec2f projRes;
	bool glReady = false, glFailed = false;
	uint64_t lastUpdateFrame = 0;
	bool scaleMeasured = false;
	float pxPerMM = 0.0f;
	ofVec2f measuredOrigin;
	int measuredCols = 0, measuredRows = 0;
	float rangeLoMM = 0.0f, rangeHiMM = 0.0f;
	ofFloatPixels fieldPix;
	ofTexture fieldTex;
	ofVboMesh mesh;
	int meshCols = 0, meshRows = 0;
	float lastFieldMs = 0.0f, lastMeshMs = 0.0f;
	char rampBuf[AsciiGlyphRenderer::MAX_RAMP + 1] = {};
	std::string presetStatus;
};
