/***********************************************************************
MyceliumNetwork.h - a hidden branching fungal network buried under the
sand, revealed where a participant digs. Two layers, kept deliberately
separate:

1. networkPattern: WHERE the mycelium physically exists, generated once
   per play area as a set of branching filaments grown outward from a
   handful of random seed points (see regenerateNetworkPattern() /
   growBranch()) - a fixed hidden layout waiting to be found, not
   something that reacts to digging itself.
2. revealedAccum: HOW MUCH of it has been uncovered so far, per cell,
   rising while that spot is currently dug and slowly decaying when it
   isn't - so filling a hole back in gradually reburies the network there
   rather than the reveal being permanent or flickering with every small
   change in hand/tool position.

"Dug" is detected the same way PuckTracker detects a raised puck (a
Gaussian-blurred local average compared against the actual elevation),
just with the sign flipped: a depression is a cell sitting meaningfully
BELOW its own blurred surroundings, rather than above them.

The combined result (pattern x revealed, per cell) is uploaded each frame
as a single-channel texture that SandSurfaceRenderer's heightMapShader
samples to blend in a glowing filament overlay - see the shader's
myceliumSampler uniform and the grid-space transform exposed here
(getGridOrigin()/getGridStep()) for converting the shader's kinect-pixel
coordinate into a texel lookup.

Ecosystem extension, part of the Shifting Sands fork of Magic Sand.
***********************************************************************/

#pragma once
#include "ofMain.h"
#include "ofxOpenCv.h"
#include "ofxCv.h"

#include "../KinectProjector/KinectProjector.h"

class MyceliumNetwork {
public:
	void setup(std::shared_ptr<KinectProjector> const& k);
	// Regenerates the buried network layout (only) when the play area's
	// grid dimensions actually change - an ROI update that doesn't change
	// cols/rows (the common case once calibrated) leaves the buried
	// network and any dig progress untouched.
	void setKinectROI(ofRectangle & KROI);
	void update();
	void drawGui();

	ofTexture & getTexture() { return combinedTex; }
	// Kinect-pixel-space origin and step size of the grid the texture
	// represents, for converting a shader's kinect pixel coordinate into
	// a texel lookup: (kinectCoord - getGridOrigin()) / getGridStep().
	ofVec2f getGridOrigin() const { return ofVec2f(kinectROI.x, kinectROI.y); }
	float getGridStep() const { return (float)step; }

	// Tunable in the debug GUI.
	static float DIG_THRESHOLD;          // mm below local surroundings to count as "dug"
	static float REVEAL_RISE_RATE;       // per second, how fast a dug cell reveals its mycelium
	static float REVEAL_DECAY_RATE;      // per second, how fast an un-dug (reburied) cell fades
	static int NETWORK_SEED_COUNT;
	static float NETWORK_BRANCH_CHANCE;  // 0..1 chance a branch forks at each grown segment
	static float NETWORK_MAX_SEGMENT_CELLS;
	static ofColor GLOW_COLOR;

private:
	void regenerateNetworkPattern();
	void growBranch(ofVec2f pos, float angle, float remainingLength, int depth);

	std::shared_ptr<KinectProjector> kinectProjector;
	ofRectangle kinectROI;
	int step, cols, rows;
	bool patternGenerated;

	cv::Mat elevationGrid, blurredGrid;
	cv::Mat networkPattern; // static per-ROI - where mycelium physically exists underground
	cv::Mat revealedAccum;  // persistent - how much of it has been dug up, per cell, 0..1

	ofTexture combinedTex;
};
