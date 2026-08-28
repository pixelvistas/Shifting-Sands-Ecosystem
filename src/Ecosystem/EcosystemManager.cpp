#include "EcosystemManager.h"

void EcosystemManager::setup(std::shared_ptr<KinectProjector> const& k, PuckTracker* tracker)
{
	hydrologyLayer.setup(k, tracker);
}

void EcosystemManager::setProjectorRes(ofVec2f & PR)
{
	hydrologyLayer.setProjectorRes(PR);
}

void EcosystemManager::setKinectRes(ofVec2f & KR)
{
	hydrologyLayer.setKinectRes(KR);
}

void EcosystemManager::setKinectROI(ofRectangle & KROI)
{
	hydrologyLayer.setKinectROI(KROI);
}

void EcosystemManager::update()
{
	hydrologyLayer.update();
}

void EcosystemManager::drawMainWindow(float x, float y, float width, float height)
{
	hydrologyLayer.drawMainWindow(x, y, width, height);
}

void EcosystemManager::drawProjectorWindow()
{
	hydrologyLayer.drawProjectorWindow();
}

void EcosystemManager::drawGui()
{
	hydrologyLayer.drawGui();
}
