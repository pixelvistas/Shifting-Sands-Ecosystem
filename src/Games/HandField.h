/***********************************************************************
HandField.h - detects hands intruding over the sand and exposes them as
impassable geometry for Critters and Tangibles.

A hand reads as raw kinect depth suddenly much closer to the sensor than
the temporally-stabilised (filtered) depth at the same pixel - the
filtered image lags behind fast changes on purpose, so it still holds
"where the sand was" for a few frames while a hand passes over it.

Ecosystem extension, part of the Shifting Sands fork of Magic Sand.
***********************************************************************/

#pragma once
#include "ofMain.h"
#include "ofxOpenCv.h"
#include "ofxCv.h"

#include "../KinectProjector/KinectProjector.h"

class HandField {
public:
	void setup(std::shared_ptr<KinectProjector> const& k);
	void update();
	void draw(float x, float y, float width, float height); // debug overlay of the mask

	// x, y in kinect pixel coordinates
	bool isInHand(float x, float y) const;
	// Outward push direction (away from the interior of the hand blob),
	// zero vector where there is no hand. Not normalized - magnitude grows
	// with distance from the blob edge, so it's already usable as a force.
	ofVec2f pushDirection(float x, float y) const;

	// How fast and which way the hand (its mask centroid) moved this frame,
	// in kinect pixels/frame. Zero when no hand is present, or on the frame
	// a hand first appears/disappears (avoids a spurious jump).
	ofVec2f getHandVelocity() const { return handVelocity; }
	// Nudge toward the hand's current direction of travel, falling off to
	// zero at INFLUENCE_RADIUS kinect pixels from the nearest hand pixel -
	// saturates to full strength at and inside the hand's own footprint, so
	// this alone covers direct contact too, not just nearby cells. Not
	// normalized - callers scale by their own strength constant.
	ofVec2f herdForce(float x, float y) const;

	// Depth difference (raw vs. filtered) above which a pixel counts as "hand".
	static float THRESHOLD;
	// How far (kinect pixels) a moving hand's influence reaches beyond its
	// own footprint.
	static float INFLUENCE_RADIUS;

private:
	std::shared_ptr<KinectProjector> kinectProjector;

	bool gridCoordAt(float x, float y, int & gx, int & gy) const;

	cv::Mat mask;     // CV_8UC1, 255 where a hand is detected
	cv::Mat distField; // CV_32F, distance (in grid cells) to nearest non-hand cell
	cv::Mat freeDistField; // CV_32F, distance (in grid cells) from free cells to nearest hand cell

	ofVec2f handCentroid, prevHandCentroid;
	ofVec2f handVelocity;
	bool hadHandLastFrame;

	ofRectangle kinectROI;
	int step; // kinect pixels per grid cell
	int cols, rows;
};
