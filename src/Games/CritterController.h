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

class CCritterController
{
public:
	void setup(std::shared_ptr<KinectProjector> const& k);
	void setProjectorRes(ofVec2f & PR);
	void setKinectRes(ofVec2f & KR);
	void setKinectROI(ofRectangle & KROI);

	void update();
	void drawMainWindow(float x, float y, float width, float height);
	void drawProjectorWindow();
	void drawGui();

	void addCritters(int n);
	void addTangible();

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
	std::vector<Critter> critters;
	std::vector<Tangible> tangibles;

	ofFbo fbo;

	int draggedTangible; // index into tangibles, -1 if not dragging
	bool showHandDebug;
	bool gradientFlipped;
};
