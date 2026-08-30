/***********************************************************************
CritterController.h - owns the Critter/Tangible/HandField population:
spawning, the per-frame update, drawing into the projector and main
windows, and a debug GUI for the parameters the physics depends on
(GRADIENT_SIGN above all - see Critter.h).

Deliberately not a CBoidGameController-style state machine (splash
screen / countdown / scores): this is a persistent ecosystem layer, not
a round-based game.

Ecosystem extension, part of the Shifting Sands fork of Magic Sand.
***********************************************************************/

#pragma once
#include "ofMain.h"

#include "../KinectProjector/KinectProjector.h"
#include "Critter.h"
#include "Tangible.h"
#include "HandField.h"
#include "PuckTracker.h"
#include "SonicWaveController.h"

class CCritterController
{
public:
	// tracker is owned by ofApp and shared with CSonicWaveController, so
	// both layers see one consistent puck rather than running detection
	// twice with independently-tunable/potentially-disagreeing state.
	// sonicWave is also owned by ofApp and queried (not owned) here, purely
	// to tint critters that fall inside a sonic wave ring - see
	// CSonicWaveController::isInsideAnyRing.
	void setup(std::shared_ptr<KinectProjector> const& k, PuckTracker* tracker, CSonicWaveController* sonicWave);
	void setProjectorRes(ofVec2f & PR);
	void setKinectRes(ofVec2f & KR);
	void setKinectROI(ofRectangle & KROI);

	void update();
	void drawMainWindow(float x, float y, float width, float height);
	void drawProjectorWindow();
	void drawGui();

	void addCritters(int n);
	void addTangible();
	int getCritterSpawnCount() const { return critterSpawnCount; }

	void mousePressed(int x, int y, int button);
	void mouseDragged(int x, int y, int button);
	void mouseReleased(int x, int y, int button);

private:
	ofPoint randomLocationInROI();

	std::shared_ptr<KinectProjector> kinectProjector;
	ofRectangle kinectROI;
	ofRectangle playArea; // so critters never reach the true edge
	ofVec2f projRes, kinectRes;

	HandField handField;
	PuckTracker* puckTracker;
	CSonicWaveController* sonicWaveController;
	std::vector<Critter> critters;
	std::vector<Tangible> tangibles;

	ofFbo fbo;

	int draggedTangible; // index into tangibles, -1 if not dragging
	bool showHandDebug;
	bool gradientFlipped;
	int critterSpawnCount; // tunable in the GUI - how many addCritters() adds per click/keypress
	float bodyColorRGB[3]; // 0..1 mirror of Critter::BODY_COLOR, kept in sync - ImGui::ColorEdit3 needs float components
};
