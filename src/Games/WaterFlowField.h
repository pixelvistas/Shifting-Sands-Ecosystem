/***********************************************************************
WaterFlowField.h - a dense vector-field-of-arrows visualization of
VegetationField's real simulated water transport (stepFlow()), REPLACING
the discrete advected-particle approach this file started as
(WaterFlowParticles, see CLAUDE.md's 2026-10-19 entries for that
history).

The particle version was built on a mistaken assumption about which
visualization convention this project was targeting: it read as a
sparse field of tracked, advected dots - after a round of flicker/
respawn fixes, the user's own direct description was "something similar
to the original critters we created - a bunch of points that respond to
gravity (somewhat)." That's the wrong metaphor entirely. The actual
target, confirmed directly by the user and consistent with Liu's own
paper (quoted in CLAUDE.md's "Future phase" section: water flow
"visualized in Grasshopper as flow-direction/velocity/vorticity vector
fields"), is a dense field of small arrow/tick glyphs covering the
water, each showing the LOCAL flow direction and relative speed at that
point, recomputed fresh every frame - the standard scientific-
visualization convention for a vector field, not a particle-tracking
system. This class has no persistent per-glyph state at all (no
position to track, no respawn/liveness logic needed) - every glyph is
just a direct, stateless read of VegetationField::getFlowVelocityAt()
at a fixed grid-space sample point, redrawn from scratch each frame.

Still its own class, same precedent as EcologyParams and the particle
version before it - it only ever reads VegetationField's state
(getWaterDepthAt()/getFlowVelocityAt(), both query-only, added
alongside the particle version and reused unchanged here) and can be
retuned/removed with zero risk to the actual simulation. Still follows
CCritterController's setup()/setProjectorRes()/setKinectROI()/update()/
drawMainWindow()/drawProjectorWindow()/drawGui() shape and FBO-
compositing pattern.

Cost: a grid of sample points spaced GLYPH_SPACING_CELLS apart (not one
query per Kinect pixel - that would be tens of thousands of arrows,
visually unreadable as well as needlessly expensive), each one
isWaterAt() check + one getFlowVelocityAt() query (unchanged 4-neighbor
read) - at the default 8-cell spacing on a ~519x295 grid that's under
2500 sample points, each doing simple arithmetic with no trig/random at
volume, the same cheap operation class the particle version's own cost
budget already confirmed safe.

Ecosystem extension, part of the Shifting Sands fork of Magic Sand.
***********************************************************************/

#pragma once
#include "ofMain.h"

#include "../KinectProjector/KinectProjector.h"
#include "VegetationField.h"

class WaterFlowField
{
public:
	// vegetationField is owned by ofApp and shared here (and with
	// CCritterController/SandSurfaceRenderer) as a query-only pointer -
	// same ownership convention as CCritterController::setup().
	void setup(std::shared_ptr<KinectProjector> const& k, VegetationField* vegetationField);
	void setProjectorRes(ofVec2f & PR);
	void setKinectROI(ofRectangle & KROI);

	void update();
	void drawMainWindow(float x, float y, float width, float height);
	void drawProjectorWindow();
	void drawGui();

	// Runtime kill switch, same A/B-testing rationale as ENABLE_SPREAD/
	// ENABLE_FLOW - instant rollback without a rebuild if this turns out
	// to carry an unexpected per-frame cost on real hardware (untested).
	static bool ENABLE_FLOW_FIELD;
	// How many grid cells apart sample points sit, in both axes - a
	// real tunable, not just a performance knob: too dense and
	// neighboring arrows overlap into unreadable clutter, too sparse and
	// real local flow variation gets missed between samples. First-
	// guess default, same "tune once actually seen on hardware" posture
	// as every other constant here.
	static int GLYPH_SPACING_CELLS;
	// getFlowVelocityAt()'s magnitude below which a sample point is
	// treated as "no visible flow here" and skipped entirely, rather
	// than drawing a meaningless near-zero-length arrow - calm,
	// undisturbed water legitimately has little to no real surface-
	// height difference to drive flow, so skipping it is physically
	// correct, not a bug to paper over. First-guess, untested -
	// drawGui()'s live velocity readout is there specifically to set
	// this (and ARROW_LENGTH_SCALE below) from a real number instead of
	// by feel once this is actually on hardware.
	static float MIN_VELOCITY_TO_SHOW;
	// getFlowVelocityAt() returns real grid-cells/second transport
	// amounts, which can be visually tiny at single-cell granularity
	// (see VegetationField.h's own note on this) - this is a
	// VISUALIZATION-ONLY multiplier converting real velocity magnitude
	// into an arrow length in grid cells, clamped to
	// [ARROW_MIN_LEN_CELLS, ARROW_MAX_LEN_CELLS] below so a very fast
	// local flow can't draw an arrow longer than makes sense next to
	// its neighbors. Not a change to the underlying physics.
	static float ARROW_LENGTH_SCALE;
	static float ARROW_MIN_LEN_CELLS;
	static float ARROW_MAX_LEN_CELLS;
	// Glyph line thickness (px) and color (0..1 RGB) - deliberately a
	// pale near-white/cyan by default, distinguishable from water's own
	// darker blue so glyphs read as "showing flow on top of water," not
	// part of the water color itself. Live-tunable via
	// ImGui::ColorEdit3, matching deer's existing precedent.
	static float GLYPH_THICKNESS;
	static float GLYPH_COLOR[3];

private:
	// gx/gy are grid indices (see VegetationField::getCols()/getRows()),
	// NOT kinect pixel coordinates - matches how CCritterController
	// already tracks positions in grid space.
	ofPoint gridToProjCoord(float gx, float gy) const;
	ofVec2f gridToKinectCoord(float gx, float gy) const;

	// Draws one arrow glyph: a shaft from gridPos toward gridPos +
	// normalize(gridDir)*lengthCells, plus a small two-stroke arrowhead
	// at the tip - the standard vector-field glyph shape. All math is
	// done in grid space and only converted to projector space at the
	// final draw call (gridToProjCoord()), the same approach the
	// particle version used for its streak lines - this way any
	// projective warping the Kinect/projector calibration applies is
	// handled correctly for free, rather than needing a separate
	// rotation computed in projector-pixel space.
	void drawArrow(ofVec2f gridPos, ofVec2f gridDir, float lengthCells);

	std::shared_ptr<KinectProjector> kinectProjector;
	VegetationField* vegetationField; // query-only, owned by ofApp - see setup()
	ofRectangle kinectROI;
	ofVec2f projRes;

	ofFbo fbo;
};
