/***********************************************************************
PuckTracker.h - detects the physical 3D-printed puck placed on the sand
using the Kinect's own depth data, and tracks its position while it sits
there.

Deliberately NOT the same raw-vs-filtered-depth trick HandField uses:
that signal is temporal (raw suddenly differs from the slowly-adapting
filtered baseline), and a puck left sitting for more than about a
second gets folded into that baseline the same way a held hand almost
got baked in as terrain (see the KinectGrabber fix/revert history) -
except here we *want* the puck to keep being found indefinitely, not
just while it's freshly placed. So this instead looks at the current
frame's spatial shape: a local high-pass on the elevation map (each
cell minus a heavily-blurred version of itself) highlights anything
raised relative to its own immediate surroundings, independent of how
long it's been there or what the surrounding terrain looks like.
Candidate blobs are then filtered by expected size and roundness, and a
match has to persist near the same spot for CONFIRM_TIME before being
trusted - long enough that a hand held fist-like and still would be a
deliberate gesture anyway, not an accident.

All the size/height/shape thresholds are guesses tuned to a rough
"6cm diameter, 4cm tall, rounded" description - they will need
empirical tuning against the real object and real hardware, same as
GRADIENT_SIGN and the sonification height range before them. Use
draw() (a debug overlay of the raised-candidate mask) to see what it's
actually finding while tuning.

Ecosystem extension, part of the Shifting Sands fork of Magic Sand.
***********************************************************************/

#pragma once
#include "ofMain.h"
#include "ofxOpenCv.h"
#include "ofxCv.h"

#include "../KinectProjector/KinectProjector.h"

class PuckTracker {
public:
	void setup(std::shared_ptr<KinectProjector> const& k);
	void update();
	void draw(float x, float y, float width, float height); // debug overlay

	bool isPuckPresent() const { return confirmed; }
	// Valid only when isPuckPresent() is true. Kinect pixel coordinates.
	const ofPoint & getPuckLocation() const { return trackedLocation; }

	// Tunable in the debug GUI - see the header note on why these need
	// empirical tuning rather than being derived.
	static float EXPECTED_RADIUS_CELLS; // grid cells (see GRID_STEP in .cpp)
	static float HEIGHT_THRESHOLD;      // elevation units above local surroundings to count as "raised"
	static float MIN_CIRCULARITY;       // 0..1, 1.0 = perfect circle; lower tolerates a rougher blob outline
	static float CONFIRM_TIME;          // seconds a candidate must persist near the same spot before being trusted
	static float MAX_TRACK_JUMP;        // kinect pixels/frame - beyond this a match is treated as a new candidate, not the same puck moving
	static int MAX_LOST_FRAMES;         // frames of no match tolerated before confirmation is dropped (detection noise grace period)
	static bool INVERT_ELEVATION;       // flip if a real raised puck isn't triggering the height threshold

private:
	std::shared_ptr<KinectProjector> kinectProjector;

	ofRectangle kinectROI;
	int step;
	int cols, rows;

	cv::Mat elevationGrid, blurredGrid, raisedMask;

	bool candidateActive;
	ofPoint candidateLocation;
	float candidateAge;
	int lostFrames;

	bool confirmed;
	ofPoint trackedLocation;
};
