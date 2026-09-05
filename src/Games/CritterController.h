/***********************************************************************
CritterController.h - the ELF population controller: owns both species
(Critter = BDdeer, HumanAgent = BDagent), the per-tick update including
spawn/death bookkeeping (BDenvironment.stepDeer()/stepAgents()'s
newlist/it.remove() pattern), drawing into the projector and main
windows, and a debug GUI for both species' economies.

No HandField, Tangible, PuckTracker, or CSonicWaveController here anymore
- those belonged to this fork's earlier slope-gravity critter, which had
no ELF equivalent and has been retired (see Critter.h's header note).
Deer and humans interact with the sand only through VegetationField
(what's grown where, water/snow, grid size) exactly as BDenvironment's
agents interact only with its BDlocation grid - not with a hand, a
dragged prop, a physical puck, or a sonic ring, none of which exist in
ELF.

Deliberately not a CBoidGameController-style state machine (splash
screen / countdown / scores): this is a persistent ecosystem layer, not
a round-based game.

Ecosystem extension, part of the Shifting Sands fork of Magic Sand.
***********************************************************************/

#pragma once
#include "ofMain.h"

#include "../KinectProjector/KinectProjector.h"
#include "Critter.h"
#include "HumanAgent.h"
#include "VegetationField.h"

class CCritterController
{
public:
	// vegetationField is owned by ofApp and shared here (and with
	// SandSurfaceRenderer) - same query-only-pointer pattern used
	// elsewhere in this codebase (see PuckTracker's use by two layers).
	void setup(std::shared_ptr<KinectProjector> const& k, VegetationField* vegetationField);
	void setProjectorRes(ofVec2f & PR);
	void setKinectROI(ofRectangle & KROI);

	void update();
	void drawMainWindow(float x, float y, float width, float height);
	void drawProjectorWindow();
	void drawGui();

	void addDeer(int n);
	void addHumans(int n);
	int getDeerSpawnCount() const { return deerSpawnCount; }
	int getHumanSpawnCount() const { return humanSpawnCount; }

private:
	ofPoint gridToProjCoord(int gx, int gy) const;
	int randomCol() const;
	int randomRow() const;

	std::shared_ptr<KinectProjector> kinectProjector;
	VegetationField* vegetationField; // query-only, owned by ofApp - see setup()
	ofRectangle kinectROI;
	ofVec2f projRes;

	std::vector<Critter> deer;
	std::vector<HumanAgent> humans;

	ofFbo fbo;

	int deerSpawnCount;    // tunable in the GUI - how many addDeer() adds per click
	int humanSpawnCount;   // tunable in the GUI - how many addHumans() adds per click
	int maxDeerPopulation;  // adapted from ELF's literal MAXDEERS=1000, which assumes no per-pair
	int maxHumanPopulation; // collision/rendering cost - see drawGui()'s note
	float deerColorRGB[3];   // 0..1 mirror of Critter::BODY_COLOR, kept in sync for ImGui::ColorEdit3
};
