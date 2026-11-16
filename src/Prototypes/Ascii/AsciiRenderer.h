/***********************************************************************
AsciiRenderer - Prototype 3 ("ASCII") display module.

Draws the sandbox state as a grid of text characters, upright in
projector space, on black:
  - simulated water: animated ~ - = waves, cyan, brighter with depth;
  - contour lines every CONTOUR_INTERVAL_MM (from CONTOUR_OFFSET_MM): - | / \ along the contour;
  - plants (Site feedback model): one glyph set per successional role
    (pioneer , ; v / intermediate + x X / climax % & #), denser glyph for
    higher cover;
  - bare land: a density ramp ( . : o O @) by elevation;
  - snow (ELF model only): *.

Two passes, own shaders (bin/data/shaders/ascii/GL2|GL3):
  1. asciiCells: the per-Kinect-cell data texture (R elevation mm,
     G water depth mm / -1 snow / 0 dry, B plant code, A valid) is drawn
     through the projector calibration (KinectProjector::
     kinectCoordToProjCoord on a coarse grid, as Prototype 2 does) into
     a float FBO with one texel per character cell; each texel aggregates
     a 3 x 3 footprint of Kinect cells.
  2. ascii: a projector-sized quad picks a glyph per cell from that FBO
     and its four neighbours (contours, gradient direction) and samples
     the glyph atlas (AsciiFont).

Read-only against KinectProjector, VegetationField and SuccessionModel;
the water and succession simulations keep running whatever is shown.
In "ASCII" display mode ofApp draws only this module's FBO (opaque).
Removing the module means deleting src/Prototypes/Ascii,
bin/data/shaders/ascii, the .vcxproj(.filters) entries and the
"Display mode" ASCII lines in ofApp.
***********************************************************************/

#pragma once
#include "ofMain.h"

class KinectProjector;
class VegetationField;

class AsciiRenderer
{
public:
	void setup(std::shared_ptr<KinectProjector> const & kp, const VegetationField * vf);
	void setProjectorRes(const ofVec2f & res);
	// Rebuilds the character grid and redraws the FBO. Call only while
	// the module is shown.
	void update();
	// Opaque: black and the characters.
	void drawMainWindow(float x, float y, float w, float h);
	void drawProjectorWindow();
	void drawGui();

	static int CELL_PX;                 // character cell size in projector pixels
	static float LAND_MIN_MM;           // elevation of the sparsest land glyph
	static float LAND_MAX_MM;           // elevation of the densest land glyph
	static bool SHOW_CONTOURS;
	static float CONTOUR_INTERVAL_MM;
	static float CONTOUR_OFFSET_MM;
	static bool CONTOURS_OVER_PLANTS;
	static bool SHOW_PLANTS;
	static float PLANT_MIN_COVER;       // below this the land glyph shows
	static float WATER_MIN_FRACTION;    // share of a cell's samples that must be water
	static float WATER_FULL_MM;         // depth at full water brightness
	static float WAVE_SPEED;            // wave phase cycles per second
	static float WAVE_LENGTH_CELLS;
	static float LAND_DIM;              // brightness of the lowest land glyph (highest = 1)
	static float BRIGHTNESS;
	static ofFloatColor LAND_COLOR;
	static ofFloatColor CONTOUR_COLOR;
	static ofFloatColor WATER_COLOR;
	static ofFloatColor PLANT_COLOR[3]; // pioneer, intermediate, climax
	static ofFloatColor SNOW_COLOR;

private:
	void allocateCells();
	void uploadData();
	void rebuildMesh();
	void renderFbo();
	void composite(float x, float y, float w, float h);

	std::shared_ptr<KinectProjector> kinectProjector;
	const VegetationField * vegetation = nullptr;
	ofVec2f projRes;
	int cellCols = 0, cellRows = 0, allocatedCellPx = 0;
	ofFbo cellFbo;   // one RGBA32F texel per character cell
	ofFbo fbo;       // projector resolution, the composited output
	ofShader cellShader, asciiShader;
	bool shadersLoaded = false;
	ofFloatPixels dataPix;
	ofTexture dataTex;
	ofTexture glyphTex;
	ofVboMesh mesh;
	int meshCols = 0, meshRows = 0;
	ofMesh quad;
	bool hasFrame = false;
	float lastUploadMs = 0.0f, lastMeshMs = 0.0f;
	int waterCells = 0, plantCells = 0;
};
