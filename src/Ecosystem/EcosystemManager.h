/***********************************************************************
EcosystemManager.h - owns the ecosystem simulation layers and enforces
the update order ECOSIMSPEC.md §4 specifies ("order matters"). Owns
HydrologyLayer (build order step 1) and VegetationLayer (step 4, single
species) - SoilLayer and the rest of the build order slot in here later,
in the fixed sequence §4 lays out, without ofApp needing to change
anything beyond what it already calls.

The ordering this class exists to enforce is now real, not hypothetical:
update() must call hydrologyLayer.update() before vegetationLayer.update()
every frame, since vegetation reads the moisture hydrology deposited -
see VegetationLayer::update()'s HydrologyLayer& parameter.

Ecosystem module, part of the Shifting Sands fork of Magic Sand.
***********************************************************************/

#pragma once
#include "ofMain.h"

#include "../KinectProjector/KinectProjector.h"
#include "../Games/PuckTracker.h"
#include "HydrologyLayer.h"
#include "VegetationLayer.h"

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
	VegetationLayer vegetationLayer;
};
