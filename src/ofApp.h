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
#include "Games/WaterFlowField.h"
#include "Common/DitherFilter.h"
#include "Common/AsciiFilter.h"

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
	// Visual-only layer: particles running downhill over the smoothed
	// terrain gradient, with fading trails - see WaterFlowField.h. Query-only
	// against vegetationField, same ownership convention as
	// critterController.
	WaterFlowField waterFlowField;
	// Optional screen-space filters over the Sandbox projector output,
	// display only, mutually exclusive: one setting, Off / Dither / ASCII
	// (src/Common/DitherFilter.h, src/Common/AsciiFilter.h). Each filter's
	// own "on" flag follows it. ASCII by default on this branch. Saved,
	// with the ASCII filter's Sandbox inputs, in settings/sandboxFilter.xml
	// whenever it changes.
	enum { FILTER_OFF = 0, FILTER_DITHER = 1, FILTER_ASCII = 2 };
	int screenFilter = FILTER_ASCII;
	bool asciiReplacesLines = true; // band outlines replace the Sandbox's own lines
	float asciiBandMarginMM = 1.5f; // band hysteresis for the parts (SandSurfaceRenderer)
	float asciiNormalRadiusPx = 5.0f; // surface normals: half-width of the differences, Kinect px
	DitherFilter sandboxDither;
	AsciiFilter sandboxAscii;
	bool ditherActive() const { return screenFilter == FILTER_DITHER && !sandboxDither.shadersFailed(); }
	bool asciiActive() const { return screenFilter == FILTER_ASCII && !sandboxAscii.shadersFailed(); }
	void syncFilterFlags();
	void drawFilterGui();
	bool drawAsciiProjector();
	void loadFilterSettings();
	void saveFilterSettings();

	// Main window ROI
	ofRectangle mainWindowROI;
	// Last-seen kinect ROI, purely to detect when it changes (the ROI
	// calibration can move mid-session) - previously this diffed against
	// mapGameController's own cached copy, which is gone along with it.
	ofRectangle lastKinectROI;

	// True once vegetationField's elevation range has been auto-seeded
	// from a real (post-"RUN!") Magic Sand calibration - see the
	// kinectROI-change block in update(). Guards against re-applying the
	// calibrated-ceiling default on every later ROI change, which would
	// silently overwrite whatever the user has since tuned by hand in the
	// Vegetation panel's live elevation-range sliders.
	bool elevationRangeAutoApplied = false;
};
