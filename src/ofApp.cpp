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

	waterFlowField.setup(kinectProjector, &vegetationField);
	waterFlowField.setProjectorRes(projRes);
	vegetationField.setProjectorRes(projRes);
	waterFlowField.setKinectROI(kinectROI);

	lastKinectROI = kinectROI;

	// Sandbox dither filter (display only): its own preset file (Save/Load
	// in the Sand Surface Renderer panel); on by default on this branch.
	// GL objects are made on first use, in the projector window.
	sandboxDither.setPresetFile("settings/sandboxDither.xml");
	sandboxDither.setClassNames("Pioneer", "Intermediate", "Climax");
	if (ofFile::doesFileExist("settings/sandboxDither.xml"))
		sandboxDither.loadPreset();
	// Sandbox ASCII filter (display only), the same way: its own preset file,
	// GL objects and font atlas made on first use in the projector window.
	// Shimmer classes are the parts' layer field (SandSurfaceRenderer).
	sandboxAscii.setPresetFile("settings/sandboxAscii.xml");
	sandboxAscii.setClassNames("Bare land", "Plants", "Water");
	if (ofFile::doesFileExist("settings/sandboxAscii.xml"))
		sandboxAscii.loadPreset();
	// The one filter setting (Off / Dither / ASCII) overrides the presets'
	// own "on" flags.
	loadFilterSettings();
	syncFilterFlags();

	imgui.setup();

}


void ofApp::update() {
    // Call kinectProjector->update() first during the update function()
	kinectProjector->update();
	// Display only: with the dither filter on, the sand is drawn for it
	// (flat plants + stage code in alpha, solid contour diagonals).
	sandSurfaceRenderer->setDitherFilterActive(ditherActive());
	// ASCII filter on: flat plants, the parts pass, and (by default) no
	// Sandbox lines in the colour (the filter's band outlines replace them).
	sandSurfaceRenderer->setAsciiFilterActive(asciiActive(), asciiReplacesLines);
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
		waterFlowField.setKinectROI(kinectROI);
	}

	vegetationField.update();
	critterController.update();
	waterFlowField.update();
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
		if (ditherActive() || asciiActive()) {
			// The sand's alpha carries the stage code (252..255): copy it,
			// do not blend it (the main window shows it unfiltered).
			ofPushStyle();
			ofEnableBlendMode(OF_BLENDMODE_DISABLED);
			sandSurfaceRenderer->drawMainWindow(x, y, w, h);
			ofPopStyle();
		} else {
			sandSurfaceRenderer->drawMainWindow(x, y, w, h);
		}
		vegetationField.drawSeedsMainWindow(x, y, w, h);
		waterFlowField.drawMainWindow(x, y, w, h);
		critterController.drawMainWindow(x, y, w, h);
	}

	kinectProjector->drawMainWindow(x, y, w, h);

	imgui.begin();
		kinectProjector->drawGui();
		if (kinectProjector->GetApplicationState() == KinectProjector::APPLICATION_STATE_RUNNING) {
			sandSurfaceRenderer->drawGui();
			// Appends to the Sand Surface Renderer panel (same window name).
			ImGui::Begin("Sand Surface Renderer");
			drawFilterGui();
			ImGui::End();
			critterController.drawGui();
			vegetationField.drawGui();
			waterFlowField.drawGui();
		}
	imgui.end();
	imgui.draw();
}

void ofApp::drawProjWindow(ofEventArgs &args)
{
	if (kinectProjector->GetApplicationState() == KinectProjector::APPLICATION_STATE_RUNNING)
	{
		// Sandbox dither filter (display only): the same four layers,
		// captured in a projector-sized FBO and drawn as dots. The sand is
		// copied without blending, so its alpha (the stage code for the
		// filter's dot size) arrives intact; the layers after it write
		// colour only (glColorMask), so their own blend functions cannot
		// change that alpha. Off (or shaders missing): straight to the
		// window, exactly as before.
		// Sandbox ASCII filter (display only): the sand FBO is the colour,
		// its parts FBO the parts, and seeds, Dewitz particles and critters
		// hand over the FBOs they already draw into as mark layers instead
		// of compositing them. False (shaders or font missing): drawn as
		// with the filter off.
		const bool ascii = asciiActive() && drawAsciiProjector();
		const bool dither = !ascii && ditherActive()
			&& sandboxDither.begin((int)projWindow->getWidth(), (int)projWindow->getHeight());
		if (ascii) {
			// drawn
		} else if (dither) {
			ofPushStyle();
			ofEnableBlendMode(OF_BLENDMODE_DISABLED);
			sandSurfaceRenderer->drawProjectorWindow();
			ofPopStyle();
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
			vegetationField.drawSeedsProjectorWindow();
			waterFlowField.drawProjectorWindow();
			critterController.drawProjectorWindow();
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			sandboxDither.end();
			sandboxDither.drawCaptured(0, 0);
		} else {
			sandSurfaceRenderer->drawProjectorWindow();
			vegetationField.drawSeedsProjectorWindow();
			waterFlowField.drawProjectorWindow();
			critterController.drawProjectorWindow();
		}
	}
	kinectProjector->drawProjectorWindow(); // calibration and overlays, never filtered
}

bool ofApp::drawAsciiProjector()
{
	const ofTexture * parts = sandSurfaceRenderer->getPartsTexture();
	if (!parts)
		return false;
	const ofTexture & colour = sandSurfaceRenderer->getProjectorTexture();
	sandboxAscii.beginMarks((int)colour.getWidth(), (int)colour.getHeight());
	// Later layers replace earlier ones where they show (critters on top).
	if (const ofTexture * t = vegetationField.getSeedMarkTexture())
		sandboxAscii.addMarks(*t, SeedLayer::MARK_GLYPH, true);       // premultiplied
	if (const ofTexture * t = waterFlowField.getMarkTexture())
		sandboxAscii.addMarks(*t, WaterFlowField::MARK_GLYPH, true);  // additive RGB
	if (const ofTexture * t = critterController.getMarkTexture())
		sandboxAscii.addMarks(*t, CCritterController::MARK_GLYPH, false); // straight alpha
	return sandboxAscii.draw(colour, *parts, 0, 0);
}

// Each filter's own "on" flag follows the one setting.
void ofApp::syncFilterFlags()
{
	screenFilter = std::max(0, std::min(2, screenFilter));
	sandboxDither.params.enabled = screenFilter == FILTER_DITHER;
	sandboxAscii.params.enabled = screenFilter == FILTER_ASCII;
}

static const char * MARK_GLYPHS[] = { ".", "+", "*", "o", "x", "~", "'", "^", "#", "@" };
static const int MARK_GLYPH_COUNT = 10;

static bool markGlyphCombo(const char * label, char & glyph)
{
	int item = 0;
	for (int i = 0; i < MARK_GLYPH_COUNT; i++)
		if (MARK_GLYPHS[i][0] == glyph)
			item = i;
	if (!ImGui::Combo(label, &item, MARK_GLYPHS, MARK_GLYPH_COUNT))
		return false;
	glyph = MARK_GLYPHS[item][0];
	return true;
}

// Inside the Sand Surface Renderer panel: the one filter setting, then each
// filter's own section. A filter's own "on" checkbox (or a preset loaded
// with it on or off) changes the setting too.
void ofApp::drawFilterGui()
{
	const int before = screenFilter;
	ImGui::Separator();
	static const char * filters[] = { "Off", "Dither", "ASCII" };
	ImGui::Combo("Screen filter", &screenFilter, filters, 3);
	ImGui::Text("(display only; calibration overlays are never filtered)");
	syncFilterFlags();
	const bool d0 = sandboxDither.params.enabled, a0 = sandboxAscii.params.enabled;
	sandboxDither.drawGui();
	sandboxAscii.drawGui();
	if (sandboxDither.params.enabled != d0)
		screenFilter = sandboxDither.params.enabled ? FILTER_DITHER : FILTER_OFF;
	else if (sandboxAscii.params.enabled != a0)
		screenFilter = sandboxAscii.params.enabled ? FILTER_ASCII : FILTER_OFF;
	syncFilterFlags();
	bool changed = screenFilter != before;
	ImGui::PushID("AsciiSandbox");
	if (ImGui::CollapsingHeader("ASCII filter: Sandbox inputs")) {
		changed |= ImGui::Checkbox("Band outlines replace the Sandbox lines", &asciiReplacesLines);
		ImGui::Text("(off: land, water and shore lines stay in the colour, for comparison)");
		ImGui::Text("Mark glyphs (drawn instead of the layer):");
		changed |= markGlyphCombo("Seeds", SeedLayer::MARK_GLYPH);
		changed |= markGlyphCombo("Flow particles (Dewitz)", WaterFlowField::MARK_GLYPH);
		changed |= markGlyphCombo("Deer and humans", CCritterController::MARK_GLYPH);
		ImGui::Text("(older particle modes draw nothing under ASCII)");
	}
	ImGui::PopID();
	if (changed)
		saveFilterSettings();
}

void ofApp::loadFilterSettings()
{
	ofXml xml;
	if (!xml.load("settings/sandboxFilter.xml"))
		return;
	auto s = xml.getChild("SANDBOXFILTER");
	if (!s)
		return;
	if (s.getChild("filter")) screenFilter = s.getChild("filter").getIntValue();
	if (s.getChild("asciiReplacesLines")) asciiReplacesLines = s.getChild("asciiReplacesLines").getIntValue() != 0;
	auto glyph = [&](const char * name, char & g) {
		auto c = s.getChild(name);
		if (!c)
			return;
		const int v = c.getIntValue();
		if (v >= 33 && v <= 126)
			g = (char)v;
	};
	glyph("seedGlyph", SeedLayer::MARK_GLYPH);
	glyph("particleGlyph", WaterFlowField::MARK_GLYPH);
	glyph("critterGlyph", CCritterController::MARK_GLYPH);
}

void ofApp::saveFilterSettings()
{
	ofXml xml;
	auto s = xml.appendChild("SANDBOXFILTER");
	s.appendChild("filter").set(screenFilter);
	s.appendChild("asciiReplacesLines").set(asciiReplacesLines ? 1 : 0);
	s.appendChild("seedGlyph").set((int)(unsigned char)SeedLayer::MARK_GLYPH);
	s.appendChild("particleGlyph").set((int)(unsigned char)WaterFlowField::MARK_GLYPH);
	s.appendChild("critterGlyph").set((int)(unsigned char)CCritterController::MARK_GLYPH);
	xml.save("settings/sandboxFilter.xml");
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
