/***********************************************************************
EcosystemManager.h - owns the ecosystem simulation layers and enforces
the update order ECOSIMSPEC.md §4 specifies ("order matters"). Currently
owns only HydrologyLayer (build order step 1, §9) - VegetationLayer,
SoilLayer, and the seed bank/pedogenesis/disturbance steps that follow it
in the build order slot in here later, in the fixed sequence §4 lays out,
without ofApp needing to change anything beyond what it already calls.

Thin on purpose at this stage: with a single layer there is nothing yet
to actually order, but the seam is worth having now rather than
retrofitting it once a second layer arrives needing to read this one's
output (vegetation reads moisture the hydrology layer deposits, per
§5.6) - see the module's own build order for what's still ahead.

Ecosystem module, part of the Shifting Sands fork of Magic Sand.
***********************************************************************/

#pragma once
#include "ofMain.h"

#include "../KinectProjector/KinectProjector.h"
#include "../Games/PuckTracker.h"
#include "HydrologyLayer.h"

class EcosystemManager {
public:
	// tracker is owned by ofApp and shared across every layer that needs
	// puck position - same query-only-pointer pattern as CCritterController.
	void setup(std::shared_ptr<KinectProjector> const& k, PuckTracker* tracker);
	void setProjectorRes(ofVec2f & PR);
	void setKinectRes(ofVec2f & KR);
	void setKinectROI(ofRectangle & KROI);

	void update();
	void drawMainWindow(float x, float y, float width, float height);
	void drawProjectorWindow();
	void drawGui();

private:
	HydrologyLayer hydrologyLayer;
};
