#include "EcosystemManager.h"

void EcosystemManager::setup(std::shared_ptr<KinectProjector> const& k, PuckTracker* tracker)
{
	hydrologyLayer.setup(k, tracker);
	vegetationLayer.setup(k);
}

void EcosystemManager::setProjectorRes(ofVec2f & PR)
{
	hydrologyLayer.setProjectorRes(PR);
	vegetationLayer.setProjectorRes(PR);
}

void EcosystemManager::setKinectRes(ofVec2f & KR)
{
	hydrologyLayer.setKinectRes(KR);
}

void EcosystemManager::setKinectROI(ofRectangle & KROI)
{
	hydrologyLayer.setKinectROI(KROI);
	vegetationLayer.setKinectROI(KROI);
}

void EcosystemManager::update()
{
	// Order matters (§4): vegetation reads this frame's moisture deposits,
	// so hydrology must update first.
	hydrologyLayer.update();
	vegetationLayer.update(hydrologyLayer);
}

void EcosystemManager::drawMainWindow(float x, float y, float width, float height)
{
	hydrologyLayer.drawMainWindow(x, y, width, height);
	vegetationLayer.drawMainWindow(x, y, width, height);
}

void EcosystemManager::drawProjectorWindow()
{
	hydrologyLayer.drawProjectorWindow();
	vegetationLayer.drawProjectorWindow();
}

void EcosystemManager::drawGui()
{
	hydrologyLayer.drawGui();
	vegetationLayer.drawGui();
}
