/***********************************************************************
ofApp.cpp - main openframeworks app
Copyright (c) 2016-2017 Thomas Wolf and Rasmus R. Paulsen (people.compute.dtu.dk/rapa)

This file is part of the Magic Sand.

The Magic Sand is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the
License, or (at your option) any later version.

The Magic Sand is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License along
with the Augmented Reality Sandbox; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
***********************************************************************/

#include "ofApp.h"

void ofApp::setup() {
	// OF basics
	ofSetFrameRate(60);
	ofBackground(0);
	ofSetVerticalSync(true);
	ofSetLogLevel(OF_LOG_VERBOSE);
	ofSetLogLevel("ofThread", OF_LOG_WARNING);
	ofSetLogLevel("ofFbo", OF_LOG_ERROR);
	ofSetLogLevel("ofShader", OF_LOG_ERROR);
	ofSetLogLevel("ofxKinect", OF_LOG_WARNING);

	// Setup kinectProjector
	kinectProjector = std::make_shared<KinectProjector>(projWindow);
	kinectProjector->setup(true);

	// Setup sandSurfaceRenderer
	sandSurfaceRenderer = new SandSurfaceRenderer(kinectProjector, projWindow);
	sandSurfaceRenderer->setup(true);

	// Retrieve variables
	ofVec2f kinectRes = kinectProjector->getKinectRes();
	ofVec2f projRes = ofVec2f(projWindow->getWidth(), projWindow->getHeight());
	ofRectangle kinectROI = kinectProjector->getKinectROI();
	mainWindowROI = ofRectangle((ofGetWindowWidth()-kinectRes.x)/2, (ofGetWindowHeight()-kinectRes.y)/2, kinectRes.x, kinectRes.y);

	// vegetationField must be ready (its grid allocated) before
	// critterController's setKinectROI() below, since that's what
	// triggers the population's first spawn - see CCritterController::
	// setKinectROI()/addDeer()/addHumans().
	vegetationField.setup(kinectProjector);
	// Seed the calibration range from getCalibratedCeilingElevation() -
	// see its header note. This early it's almost certainly still reading
	// KinectProjector's hardcoded, untrained defaults rather than a real
	// per-installation calibration - startApplication() (the "RUN!"
	// button) is what actually loads basePlaneOffset/maxOffset from
	// kinectProjectorSettings.xml, and that hasn't run yet here. This
	// value gets properly refreshed in ofApp::update()'s kinectROI-change
	// block below once real calibration has actually loaded - this call
	// is just so the range is never literally unset before then. There's
	// no equivalent calibrated floor in Magic Sand (only a ceiling is
	// calibrated, for hand-rejection, not a dig-depth limit), so the
	// floor is assumed symmetric with the ceiling until measured. Live-
	// tunable afterward in the Vegetation panel either way - see
	// VegetationField.h's header note.
	float ceilingElevation = kinectProjector->getCalibratedCeilingElevation();
	vegetationField.setElevationRange(-ceilingElevation, ceilingElevation);
	vegetationField.setKinectROI(kinectROI);
	sandSurfaceRenderer->setVegetationField(&vegetationField);

	critterController.setup(kinectProjector, &vegetationField);
	critterController.setProjectorRes(projRes);
	critterController.setKinectROI(kinectROI);
	lastKinectROI = kinectROI;

	imgui.setup();

}


void ofApp::update() {
    // Call kinectProjector->update() first during the update function()
	kinectProjector->update();
   	sandSurfaceRenderer->update();

	if (kinectProjector->getKinectROI() != lastKinectROI)
	{
		ofRectangle kinectROI = kinectProjector->getKinectROI();
		lastKinectROI = kinectROI;
		// Re-pull the calibrated ceiling here too, not just at ofApp::setup() -
		// setup() runs before the "RUN!" button (KinectProjector::
		// startApplication()) ever executes, and startApplication() is what
		// actually calls loadSettings() and populates basePlaneOffset/
		// maxOffset from kinectProjectorSettings.xml. A setup()-time-only
		// call reads those fields while they're still sitting on their
		// hardcoded, untrained defaults (basePlaneOffsetBack=870,
		// maxOffsetBack=basePlaneOffset.z-300=570 -> a ceiling of exactly
		// 300, not this installation's real calibration) and never
		// refreshes, so "calibrated" ends up being a lie in practice. The
		// kinectROI changing is a reasonable proxy for "calibration state
		// just changed" (setNewKinectROI() inside startApplication() is
		// what replaces the full-sensor default ROI with the real
		// calibrated one).
		//
		// Guarded to apply only once (elevationRangeAutoApplied), not on
		// every ROI change this block fires for: confirmed on real
		// hardware that re-applying unconditionally here made the
		// Vegetation panel's elevation-range sliders look completely
		// inert (-300/300 typed in "did absolutely nothing") - almost
		// certainly because getKinectROI() != lastKinectROI was true far
		// more often than "calibration state just changed" (e.g. every
		// frame, from floating-point jitter in a live-recomputed ROI),
		// silently stomping any manual slider edit back to the calibrated
		// ceiling before the next frame ever rendered it. Once auto-
		// applied here the first time, later ROI changes leave the range
		// alone - the "Reset range to +-calibrated ceiling" button in the
		// Vegetation panel covers re-pulling it by hand if calibration is
		// redone later.
		if (!elevationRangeAutoApplied) {
			float ceilingElevation = kinectProjector->getCalibratedCeilingElevation();
			vegetationField.setElevationRange(-ceilingElevation, ceilingElevation);
			elevationRangeAutoApplied = true;
		}
		vegetationField.setKinectROI(kinectROI);
		critterController.setKinectROI(kinectROI);
	}

	vegetationField.update();
	critterController.update();
}


void ofApp::draw()
{
	float x = mainWindowROI.x;
	float y = mainWindowROI.y;
	float w = mainWindowROI.width;
	float h = mainWindowROI.height;

	if (kinectProjector->GetApplicationState() == KinectProjector::APPLICATION_STATE_RUNNING)
	{
		// Ecosystem fork: flat bare-sand base with ELF-style vegetation
		// patches blended on top (see heightMapShader.frag and
		// VegetationField.h), plus the ELF deer/human population - see
		// ofApp.h's header note on what has been removed from this branch.
		sandSurfaceRenderer->drawMainWindow(x, y, w, h);
		critterController.drawMainWindow(x, y, w, h);
	}

	kinectProjector->drawMainWindow(x, y, w, h);

	imgui.begin();
		kinectProjector->drawGui();
		if (kinectProjector->GetApplicationState() == KinectProjector::APPLICATION_STATE_RUNNING) {
			sandSurfaceRenderer->drawGui();
			critterController.drawGui();
			vegetationField.drawGui();
		}
	imgui.end();
	imgui.draw();
}

void ofApp::drawProjWindow(ofEventArgs &args)
{
	if (kinectProjector->GetApplicationState() == KinectProjector::APPLICATION_STATE_RUNNING)
	{
		sandSurfaceRenderer->drawProjectorWindow();
		critterController.drawProjectorWindow();
	}
	kinectProjector->drawProjectorWindow();
}

void ofApp::exit()
{
}

void ofApp::keyPressed(int key)
{
	if (key == 'c')
	{
		kinectProjector->SaveKinectColorImage();
	}
	else if (key == 'd')
	{
		kinectProjector->SaveFilteredDepthImage();
	}
	else if (key == ' ')
	{
		if (kinectProjector->GetApplicationState() == KinectProjector::APPLICATION_STATE_SETUP)
		{
			// Try to start the application
			kinectProjector->startApplication();
		}
	}
	else if (key == 'i')
	{
		if (kinectProjector->GetApplicationState() == KinectProjector::APPLICATION_STATE_RUNNING)
		{
			critterController.addDeer(critterController.getDeerSpawnCount());
		}
	}
}

void ofApp::keyReleased(int key) {

}

void ofApp::mouseMoved(int x, int y) {

}

void ofApp::mouseDragged(int x, int y, int button) {

	// We assume that we only use this during ROI annotation
	kinectProjector->mouseDragged(x - mainWindowROI.x, y - mainWindowROI.y, button);
}

void ofApp::mousePressed(int x, int y, int button)
{
	if (mainWindowROI.inside((float)x, (float)y))
	{
		kinectProjector->mousePressed(x-mainWindowROI.x, y-mainWindowROI.y, button);
	}
}

void ofApp::mouseReleased(int x, int y, int button) {
	// We assume that we only use this during ROI annotation
	kinectProjector->mouseReleased(x - mainWindowROI.x, y - mainWindowROI.y, button);
}

void ofApp::mouseEntered(int x, int y) {

}

void ofApp::mouseExited(int x, int y) {

}

void ofApp::windowResized(int w, int h) {

}

void ofApp::gotMessage(ofMessage msg) {

}

void ofApp::dragEvent(ofDragInfo dragInfo) {

}
