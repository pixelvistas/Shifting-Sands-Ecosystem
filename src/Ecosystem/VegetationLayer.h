/***********************************************************************
VegetationLayer.h - ECOSIMSPEC.md build order step 4: a single species
growing on moisture and slope suitability only (no soil, no seed bank -
those are steps 5-6). This is the first thing in the ecosystem module
that actually puts color on the sand, per §5.11.3's stipple design: per-
cell density is rendered as a jittered cluster of small dots, not a flat
fill, so ecotones read as a blend of individual points rather than a
smooth gradient or a hard edge.

Grid-based, cv::Mat density field - same "CPU grid instead of a GPU
texture" reasoning as HydrologyLayer's moisture grid and the old
MyceliumNetwork's pattern grid (still the closest precedent in this fork
for a persistent, resizable, per-cell accumulator). Reads moisture from
HydrologyLayer::getMoistureAt() and slope from
KinectProjector::gradientAtKinectCoord() rather than owning either.

Suitability, growth, establishment, and mortality follow §5.6 with two
simplifications appropriate to a single species and no seed bank yet:
- HSI is the geometric mean of two factors (moisture, slope) instead of
  three - no soil term until step 5.
- Propagule pressure B(c,s) is a flat 1.0 (unlimited seed rain) instead
  of a real seed bank - establishment is gated on the cell being close to
  unoccupied instead, so it still reads as "colonizing empty ground"
  rather than growth re-triggering itself every frame.

SIM_YEARS_PER_SECOND is the one deferred constant ECOSIMSPEC.md §2 flags
("set it to something plausible and move on") made concrete and GUI-
tunable, since a real sim-year per real second is both a reasonable
starting guess and something you'll want to speed up to actually watch a
sere develop instead of waiting for one.

Ecosystem module, part of the Shifting Sands fork of Magic Sand.
***********************************************************************/

#pragma once
#include "ofMain.h"
#include "ofxOpenCv.h"

#include "../KinectProjector/KinectProjector.h"
#include "HydrologyLayer.h"

class VegetationLayer {
public:
	void setup(std::shared_ptr<KinectProjector> const& k);
	void setProjectorRes(ofVec2f & PR);
	void setKinectROI(ofRectangle & KROI);

	// hydrology is read-only here (moisture lookups) - passed in each call
	// rather than stored, since VegetationLayer doesn't own it (see
	// EcosystemManager, which owns both and calls this after
	// hydrology.update() so this frame's deposits are visible immediately).
	void update(HydrologyLayer & hydrology);
	void drawMainWindow(float x, float y, float width, float height);
	void drawProjectorWindow();
	void drawGui();

private:
	float suitabilityAt(float moisture, float slope) const;
	void draw();

	std::shared_ptr<KinectProjector> kinectProjector;
	ofRectangle kinectROI;

	cv::Mat density; // CV_32F, 0..1 - V(c) for this one species
	int gridStep, cols, rows;

	ofFbo fbo;
	ofVec2f projRes;

	// SI_moist / SI_slope trapezoidal breakpoints [min, opt_lo, opt_hi, max]
	// - see §5.6. Real arrays (not 4 separate statics) specifically so
	// ImGui::SliderFloat4 can address all 4 contiguously - separate static
	// members have no guaranteed relative layout in memory. Moisture is in
	// this module's own 0..1 units (matches HydrologyLayer::getMoistureAt()'s
	// range). Slope is in gradientAtKinectCoord()'s raw, undocumented units -
	// same "verify empirically" situation as Critter::GRADIENT_SIGN, so
	// exposed as GUI sliders rather than guessed constants.
	static float SI_MOIST[4];
	static float SI_SLOPE[4];

	// Growth/establishment/mortality - see §6 "Per species", pioneer column.
	static float R_ESTAB, GROWTH_RATE, MORTALITY_RATE, K_MAX;
	static float SIM_YEARS_PER_SECOND;

	// Establishment only fires below this density - see header note on
	// why there's no real seed bank/vacancy term yet.
	static float ESTABLISH_BELOW_DENSITY;
	static float SEED_DENSITY; // density a successful establishment jumps to

	// Rendering - see ECOSIMSPEC.md §5.11.3/§5.11.8.
	static float MIN_DRAW_DENSITY;
	static float FULL_COVER_DENSITY;
	static int STIPPLE_DENSITY; // candidate points per cell at full cover
	static float SPRITE_MIN, SPRITE_MAX; // projector px
	static ofColor SPECIES_COLOR; // pioneer: bright yellow-green, per ECOSIMSPEC.md §5.11.3
};
