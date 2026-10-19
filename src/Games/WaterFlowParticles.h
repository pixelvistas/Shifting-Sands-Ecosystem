/***********************************************************************
WaterFlowParticles.h - a visual-only particle/streak layer over the
sandbox showing VegetationField's real simulated water transport
(stepFlow()) as moving streaks, rather than leaving flow to be inferred
from a static color gradient alone.

This is the "vector-field/particle-flow imagery" piece of the Liu/
ARsandbox014 succession-model methodology described in CLAUDE.md's
"Future phase" section - water-flow visualization was always meant to be
one of the three pieces of that methodology (alongside the CA spread
layer and hand-placed seeding), and particle-trail visualization of flow
was explicitly considered once before (2026-10-16, re: anvaka/fieldplay)
and correctly deferred at the time because the actual flow SIMULATION
(stepFlow()) didn't exist yet to feed it real data - a particle system
with no real physics behind it would have been decorative, "the literal
'non-simulated decorative flow particles' option" already passed over in
the Strategic Reset in favor of building real dynamics first. stepFlow()
exists now, so this is no longer decorative: every particle's motion is
read directly from VegetationField::getFlowVelocityAt(), the exact same
surface-height-difference physics stepFlow() itself runs, not a second,
separately-invented flow model.

Deliberately its own class, not folded into VegetationField or
CCritterController, following this project's established precedent
(EcologyParams is the other example) - it only ever READS
VegetationField's state (getWaterDepthAt()/getFlowVelocityAt(), both new
query-only accessors added alongside this class and never written to by
stepFlow() itself), so it can be added, removed, or retuned without any
risk to the actual simulation. Follows CCritterController's exact
setup()/setProjectorRes()/setKinectROI()/update()/drawMainWindow()/
drawProjectorWindow()/drawGui() shape and its FBO-compositing pattern
(fbo filled once per update(), drawn as-is in both windows) rather than
inventing a new rendering convention.

Cost, checked before writing anything per this project's own standing
rule (the CA spread layer's real lesson - see CLAUDE.md): a few hundred
particles, each doing one depth read and one 4-neighbor velocity query
per frame (no trig/random beyond occasional respawns) - several orders
of magnitude cheaper than the ~150K-cell, per-species passes that
actually caused that regression. PARTICLE_COUNT is GUI-tunable regardless,
same "first-guess, lower it if hardware disagrees" posture as every
other constant in this project.

Ecosystem extension, part of the Shifting Sands fork of Magic Sand.
***********************************************************************/

#pragma once
#include "ofMain.h"

#include "../KinectProjector/KinectProjector.h"
#include "VegetationField.h"

class WaterFlowParticles
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
	static bool ENABLE_FLOW_PARTICLES;
	// How many particles to simulate/draw - tunable live; the vector
	// resizes to match in update(). First-guess default, see the header
	// note's cost budget.
	static int PARTICLE_COUNT;
	// getFlowVelocityAt() returns real grid-cells/second transport
	// amounts, which can be visually tiny at single-cell granularity (a
	// slope's surface-height difference between two ADJACENT cells is
	// often far smaller than between two cells several apart) - this is a
	// VISUALIZATION-ONLY multiplier to make real flow direction legible at
	// a glance, not a change to the underlying physics. First-guess,
	// untested on real hardware - the GUI's live velocity readout (see
	// drawGui()) is there specifically so this can be tuned from a real
	// number instead of by feel.
	static float PARTICLE_SPEED_SCALE;
	// Streak line thickness (px) and color (0..1 RGB) - deliberately a
	// pale near-white/cyan by default, distinguishable from water's own
	// darker blue (waterRamp()'s two stops) so particles read as "moving
	// on top of water," not as part of the water color itself. Live-
	// tunable via ImGui::ColorEdit3, matching deer's existing precedent.
	static float PARTICLE_THICKNESS;
	static float PARTICLE_COLOR[3];

private:
	// Continuous (sub-cell) grid position, same space as
	// VegetationField::getCols()/getRows() - NOT kinect pixel coordinates;
	// gridToProjCoord() below converts to projector space for drawing,
	// mirroring CCritterController::gridToProjCoord()'s exact pattern.
	struct Particle {
		float gx, gy;
		float prevGx, prevGy;
	};

	ofPoint gridToProjCoord(float gx, float gy) const;
	// Places p at a random cell whose current depth clears
	// VegetationField::MIN_VISIBLE_WATER_DEPTH_FRAC, bounded retries - see
	// the .cpp. Leaves p parked off-grid (and therefore undrawn) if no wet
	// cell is found this attempt; update() naturally keeps retrying every
	// frame via the same depth check that triggers a respawn, so a bone-
	// dry sandbox just means no particles render until real water exists,
	// not an infinite loop or a crash.
	void respawnParticle(Particle & p);

	std::shared_ptr<KinectProjector> kinectProjector;
	VegetationField* vegetationField; // query-only, owned by ofApp - see setup()
	ofRectangle kinectROI;
	ofVec2f projRes;

	std::vector<Particle> particles;
	ofFbo fbo;
};
