/***********************************************************************
MycorrhizaRenderer - Prototype 2 ("Mycorrhiza") display module.

Owns a MycorrhizaField, feeds it each frame from read-only accessors
(VegetationField's elevation grid, water mask and mm per cell; the
SuccessionModel's cover, soil S and moisture M), and draws the field's
trace as a glowing fibrous network on black with its own shaders
(bin/data/shaders/mycorrhiza/GL2|GL3/mycorrhiza.*). The trace is mapped
onto the sand through KinectProjector::kinectCoordToProjCoord on a coarse
lookup grid (read-only use of the calibration), into an FBO at projector
resolution.

In "Mycorrhiza" display mode ofApp draws only this module's FBO (opaque:
black plus the network). "Overlay on sandbox" (debug, default off) adds
the network over the normal Sandbox view instead. Strand brightness comes
from the trace; hue from the ground under the strand (moisture, plant
cover or soil, height; MycorrhizaField::site()).

Nothing else in the app depends on this module: removing it means
deleting src/Prototypes/Mycorrhiza, bin/data/shaders/mycorrhiza, the two
.vcxproj(.filters) entries and the "Display mode" code in ofApp.

Settings preset "Prototype 2: Mycorrhiza": bin/data/settings/
mycorrhizaPreset.xml (Save/Load in the Mycorrhiza panel; loaded at
start-up when present). Defaults live in the .cpp files.

Not a simulation of fungi: a trace of participants' activity growing
through the model's moisture, soil and plants in the visual language of
mycelium (see docs/DEVLOG.md, 2026-11-12, for what a placard may claim).
***********************************************************************/

#pragma once
#include "ofMain.h"
#include "MycorrhizaField.h"

class KinectProjector;
class VegetationField;

class MycorrhizaRenderer
{
public:
	void setup(std::shared_ptr<KinectProjector> const & kp, const VegetationField * vf);
	void setProjectorRes(const ofVec2f & res);
	// Steps the field and redraws the FBO. Call only while the module is
	// shown (Mycorrhiza mode, or the debug overlay).
	void update();
	// Empty network (also on entering the mode).
	void clear();
	// Opaque: black and the network (Mycorrhiza mode).
	void drawMainWindow(float x, float y, float w, float h);
	void drawProjectorWindow();
	// Additive over whatever is already drawn (debug overlay).
	void drawOverlayMainWindow(float x, float y, float w, float h);
	void drawOverlayProjectorWindow();
	void drawGui();

	bool savePreset();
	bool loadPreset();

	static bool OVERLAY_ON_SANDBOX;   // debug only, default off
	static float GAMMA;
	static float CORE_GAIN;
	static float DETAIL;              // local contrast (0 off): thinner strands, darker gaps
	static float GLOW_RADIUS;         // texels (grid cells)
	static float GLOW_STRENGTH;
	static float BRIGHTNESS;          // "Mycorrhiza brightness"
	// Hue from the site under a strand: COLOR[0] base, [1] wet (moisture
	// between WET_LO and WET_HI), [2] living (cover or soil), [3] high and
	// dry (MycorrhizaField::HEIGHT_*_MM), [4] pale highlight at the
	// strongest strands (CORE_MIX).
	static ofFloatColor COLOR[5];
	static float BASE_WEIGHT;
	static float WET_LO, WET_HI;
	static float CORE_MIX;

private:
	void gatherInputs(MycorrhizaField::Inputs & in);
	void uploadTrace();
	void rebuildMesh();
	void renderFbo();
	void composite(float x, float y, float w, float h, bool additive);

	std::shared_ptr<KinectProjector> kinectProjector;
	const VegetationField * vegetation = nullptr;
	MycorrhizaField field;
	ofVec2f projRes;
	ofFbo fbo;
	ofShader shader;
	bool shaderLoaded = false;
	ofTexture traceTex, siteTex;
	ofPixels tracePix;
	ofVboMesh mesh;
	int meshCols = 0, meshRows = 0;
	std::vector<unsigned char> waterMask;
	bool hasFrame = false;
	float lastUploadMs = 0.0f, lastMeshMs = 0.0f;
	std::string presetStatus;
};
