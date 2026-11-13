/***********************************************************************
MycorrhizaRenderer - Prototype 2 ("Mycorrhiza") display module.

Owns a MycorrhizaField, feeds it each frame from read-only accessors
(VegetationField's elevation grid, water mask and mm per cell; the
SuccessionModel's cover, soil S and moisture M) and draws it on black into
an FBO at projector resolution. Grid positions go to the projector through
KinectProjector::kinectCoordToProjCoord on a coarse lookup grid (read-only
use of the calibration).

Network style (MycorrhizaField::STYLE):
- "Patches and hyphae" (default): the lit ground, a light-liquid that pools
  in basins (patches.frag: marbled swirl moved along the liquid's velocity
  with a flow map, lit as a glossy surface, colour by maturity) and the
  threads (hypha.vert/.frag: anti-aliased line quads, held in a GPU ring
  buffer that only receives newly grown segments; brightness fades in the
  shader), drawn into their own FBO with GL_MAX blending and added over the
  patches with one glow pass (glow.frag).
- "Physarum (previous)": the trace drawn by mycorrhiza.frag, as before.

In "Mycorrhiza" display mode ofApp draws only this module's FBO (opaque:
black plus the drawing). "Overlay on sandbox" (debug, default off) adds it
over the normal Sandbox view instead.

Nothing else in the app depends on this module: removing it means
deleting src/Prototypes/Mycorrhiza, bin/data/shaders/mycorrhiza, the
.vcxproj(.filters) entries and the "Display mode" code in ofApp.

Settings preset "Prototype 2: Mycorrhiza": bin/data/settings/
mycorrhizaPreset.xml (Save/Load in the Mycorrhiza panel; loaded at
start-up when present). Defaults live in the .cpp files.

Not a simulation of fungi: a trace of participants' activity growing
through the model's moisture, soil and plants in the visual language of
mycelium (see docs/DEVLOG.md for what a placard may claim).
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
	// Opaque: black and the drawing (Mycorrhiza mode).
	void drawMainWindow(float x, float y, float w, float h);
	void drawProjectorWindow();
	// Additive over whatever is already drawn (debug overlay).
	void drawOverlayMainWindow(float x, float y, float w, float h);
	void drawOverlayProjectorWindow();
	void drawGui();

	bool savePreset();
	bool loadPreset();

	static bool OVERLAY_ON_SANDBOX;   // debug only, default off

	// "Patches and hyphae" display.
	static float PATCH_SCALE_MM;      // texture scale
	static float PATCH_FLOW;          // flow speed
	static float PATCH_CONTRAST;
	static float PATCH_BRIGHTNESS;
	static float EDGE_COOL;           // edges shift toward the fresh colour
	static float RIM_GAIN;            // thin brighter rim
	static float STOP_POS[4];         // maturity of each colour stop
	static ofFloatColor STOP_COLOR[4];// cyan, blue-white, amber, orange
	static ofFloatColor HYPHA_PALE;
	static ofFloatColor HYPHA_WARM;   // within a few mm of a mature patch
	static float WARM_GAIN;           // blurred presence x maturity -> warmth 0..1
	static float HYPHA_BRIGHTNESS;
	static float GLOW_RADIUS_PX;
	static float GLOW_STRENGTH;
	// Liquid look: marbling, flow map along the liquid's velocity, gloss.
	static float WARP, STREAK, BANDS, RIDGE_WIDTH;
	static float FLOW_CYCLE;          // s, flow-map phase length
	static float V_MAX, H_MAX;        // encoding ranges of the flow texture (mm/s, mm)
	static float BUMP, SPEC_GAIN, SHININESS;
	static ofFloatColor TROUGH_COLOR;

	// "Physarum (previous)" display.
	static float GAMMA;
	static float CORE_GAIN;
	static float DETAIL;              // local contrast (0 off): thinner strands, darker gaps
	static float GLOW_RADIUS;         // texels (grid cells)
	static float PHYS_GLOW_STRENGTH;
	static float BRIGHTNESS;
	static ofFloatColor COLOR[5];     // base, wet, living, high and dry, strongest strands
	static float BASE_WEIGHT;
	static float WET_LO, WET_HI;
	static float CORE_MIX;

private:
	void loadShaders();
	void gatherInputs(MycorrhizaField::Inputs & in);
	void rebuildMesh();
	ofVec2f toProj(float gx, float gy) const;
	// Patches and hyphae.
	void uploadPatches();
	void syncHyphaRing();
	void writeSegment(size_t slot, const MycorrhizaField::Segment & s);
	void zeroSlot(size_t slot);
	void uploadRing(size_t slot0, size_t count);
	void buildTipMesh();
	void renderLayers();
	// Physarum.
	void uploadTrace();
	void renderPhysarum();
	void composite(float x, float y, float w, float h, bool additive);

	std::shared_ptr<KinectProjector> kinectProjector;
	const VegetationField * vegetation = nullptr;
	MycorrhizaField field;
	ofVec2f projRes;
	ofFbo fbo, hyphaFbo;
	ofShader shader, patchShader, hyphaShader, glowShader;
	bool shaderLoaded = false, layerShadersLoaded = false;
	ofVboMesh mesh;                   // grid, LUT_STEP cells apart: projector positions, texcoord = cell
	int meshCols = 0, meshRows = 0;
	std::vector<unsigned char> waterMask;
	bool hasFrame = false;
	// Patches and hyphae.
	ofTexture patchTex, warmTex, flowTex;
	ofPixels patchPix, warmPix, flowPix;
	ofVbo ringVbo;
	std::vector<glm::vec3> ringPos, ringNorm;
	std::vector<glm::vec2> ringTex;
	size_t ringCap = 0;               // segments
	uint64_t ringNextId = 0;          // next segment id not yet written
	ofVboMesh tipMesh;
	// Physarum.
	ofTexture traceTex, siteTex;
	ofPixels tracePix;
	float lastUploadMs = 0.0f, lastMeshMs = 0.0f, lastRingMs = 0.0f;
	std::string presetStatus;
};
