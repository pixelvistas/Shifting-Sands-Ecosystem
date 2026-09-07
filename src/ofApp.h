/***********************************************************************
ofApp.h - main openframeworks app
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

--- Ecosystem fork: stripped down to the ELF port and what it needs.
MapGameController/BoidGameController (the pre-ecosystem game modes and
their own hand-gesture input), PuckTracker (3D object/puck recognition),
MyceliumNetwork, and SonicWaveController (with it, SonicEngine,
SonicParticle, Tangible, and HandField) have all been removed - none of
them have an ELF equivalent. What remains is the Kinect/projector
pipeline, the ELF-style terrain+vegetation renderer, and the ELF
deer/human population.
***********************************************************************/

#pragma once

#include "ofMain.h"
#include "ofxImGui.h"
#include "KinectProjector/KinectProjector.h"
#include "SandSurfaceRenderer/SandSurfaceRenderer.h"
#include "Games/CritterController.h"
#include "Games/VegetationField.h"

class ofApp : public ofBaseApp {

public:
	void setup();

	void update();

	void draw();
	void drawProjWindow(ofEventArgs& args);
	void exit();

	void keyPressed(int key);
	void keyReleased(int key);
	void mouseMoved(int x, int y);
	void mouseDragged(int x, int y, int button);
	void mousePressed(int x, int y, int button);
	void mouseReleased(int x, int y, int button);
	void mouseEntered(int x, int y);
	void mouseExited(int x, int y);
	void windowResized(int w, int h);
	void dragEvent(ofDragInfo dragInfo);
	void gotMessage(ofMessage msg);

	std::shared_ptr<ofAppBaseWindow> projWindow;

private:
	ofxImGui::Gui imgui;
	std::shared_ptr<KinectProjector> kinectProjector;
	SandSurfaceRenderer* sandSurfaceRenderer;
	CCritterController critterController;
	// ELF-style flora grid rendered as colored ground patches - see
	// VegetationField.h. Shared with SandSurfaceRenderer (which binds its
	// texture as a shader uniform) and with critterController (deer/humans
	// query it for growth/water/snow) via query-only pointers.
	VegetationField vegetationField;

	// Main window ROI
	ofRectangle mainWindowROI;
	// Last-seen kinect ROI, purely to detect when it changes (the ROI
	// calibration can move mid-session) - previously this diffed against
	// mapGameController's own cached copy, which is gone along with it.
	ofRectangle lastKinectROI;
};
