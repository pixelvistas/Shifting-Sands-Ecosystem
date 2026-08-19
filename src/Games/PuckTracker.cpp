#include "PuckTracker.h"

float PuckTracker::PUCK_DIAMETER_MM = 78.74f; // 3.1in
float PuckTracker::PUCK_HEIGHT_MM = 63.5f;    // 2.5in

float PuckTracker::EXPECTED_RADIUS_CELLS = 8.0f; // placeholder until estimateRadiusCells() runs against real calibration
float PuckTracker::HEIGHT_THRESHOLD = PuckTracker::PUCK_HEIGHT_MM * 0.4f; // a dome's edges taper well short of its full height
float PuckTracker::MIN_CIRCULARITY = 0.6f;
float PuckTracker::CONFIRM_TIME = 0.6f;
float PuckTracker::MAX_TRACK_JUMP = 40.0f;
int PuckTracker::MAX_LOST_FRAMES = 6;
bool PuckTracker::INVERT_ELEVATION = false;

namespace {
	const int GRID_STEP = 4; // kinect pixels per grid cell, matching HandField's convention
}

void PuckTracker::setup(std::shared_ptr<KinectProjector> const& k)
{
	kinectProjector = k;
	step = GRID_STEP;
	cols = 0;
	rows = 0;
	candidateActive = false;
	candidateAge = 0.0f;
	lostFrames = 0;
	confirmed = false;
}

void PuckTracker::update()
{
	if (!kinectProjector->isImageStabilized()) {
		candidateActive = false;
		confirmed = false;
		return;
	}

	kinectROI = kinectProjector->getKinectROI();
	if (kinectROI.width <= 0 || kinectROI.height <= 0) {
		candidateActive = false;
		confirmed = false;
		return;
	}

	cols = std::max(1, (int)(kinectROI.width / step));
	rows = std::max(1, (int)(kinectROI.height / step));

	elevationGrid = cv::Mat(rows, cols, CV_32F);
	for (int gy = 0; gy < rows; gy++) {
		for (int gx = 0; gx < cols; gx++) {
			float kx = kinectROI.x + gx * step + step / 2.0f;
			float ky = kinectROI.y + gy * step + step / 2.0f;
			float elev = kinectProjector->elevationAtKinectCoord(kx, ky);
			elevationGrid.at<float>(gy, gx) = INVERT_ELEVATION ? -elev : elev;
		}
	}

	int blurCells = std::max(3, (int)(EXPECTED_RADIUS_CELLS * 2.5f));
	if (blurCells % 2 == 0) blurCells += 1; // GaussianBlur needs an odd kernel size
	cv::GaussianBlur(elevationGrid, blurredGrid, cv::Size(blurCells, blurCells), 0);

	cv::Mat raised = elevationGrid - blurredGrid;
	raisedMask = raised > HEIGHT_THRESHOLD;

	std::vector<std::vector<cv::Point>> contours;
	cv::findContours(raisedMask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

	float expectedArea = (float)CV_PI * EXPECTED_RADIUS_CELLS * EXPECTED_RADIUS_CELLS;

	int bestIdx = -1;
	double bestCircularity = -1.0;
	for (size_t i = 0; i < contours.size(); i++) {
		double area = cv::contourArea(contours[i]);
		if (area < expectedArea * 0.3 || area > expectedArea * 3.0)
			continue;

		double perimeter = cv::arcLength(contours[i], true);
		if (perimeter <= 0)
			continue;

		double circularity = 4.0 * CV_PI * area / (perimeter * perimeter);
		if (circularity < MIN_CIRCULARITY)
			continue;

		if (circularity > bestCircularity) {
			bestCircularity = circularity;
			bestIdx = (int)i;
		}
	}

	bool foundThisFrame = (bestIdx >= 0);
	ofPoint foundLocation;
	if (foundThisFrame) {
		cv::Moments m = cv::moments(contours[bestIdx]);
		float gx = (float)(m.m10 / m.m00);
		float gy = (float)(m.m01 / m.m00);
		foundLocation = ofPoint(kinectROI.x + gx * step, kinectROI.y + gy * step);
	}

	float dt = ofGetLastFrameTime();

	if (foundThisFrame) {
		lostFrames = 0;

		if (candidateActive && (foundLocation - candidateLocation).length() <= MAX_TRACK_JUMP) {
			candidateLocation = foundLocation;
			candidateAge += dt;
		} else {
			candidateActive = true;
			candidateLocation = foundLocation;
			candidateAge = 0.0f;
		}

		if (candidateAge >= CONFIRM_TIME) {
			confirmed = true;
			trackedLocation = candidateLocation;
		}
	} else {
		lostFrames++;
		if (lostFrames > MAX_LOST_FRAMES) {
			candidateActive = false;
			candidateAge = 0.0f;
			confirmed = false;
		}
		// Within the grace period: keep whatever state we had, so brief
		// single-frame detection dropouts don't flicker the puck on/off.
	}
}

float PuckTracker::estimateRadiusCells() const
{
	if (kinectROI.width <= 0)
		return EXPECTED_RADIUS_CELLS; // nothing to measure against yet

	float cx = kinectROI.getCenter().x;
	float cy = kinectROI.getCenter().y;
	const float sampleOffsetPixels = 50.0f;

	ofVec3f worldA = kinectProjector->kinectCoordToWorldCoord(cx, cy);
	ofVec3f worldB = kinectProjector->kinectCoordToWorldCoord(cx + sampleOffsetPixels, cy);
	float worldDistMM = (worldB - worldA).length();
	if (worldDistMM <= 0.0001f)
		return EXPECTED_RADIUS_CELLS; // degenerate calibration sample, don't produce garbage

	float mmPerKinectPixel = worldDistMM / sampleOffsetPixels;
	float radiusKinectPixels = (PUCK_DIAMETER_MM / 2.0f) / mmPerKinectPixel;
	return radiusKinectPixels / step;
}

void PuckTracker::draw(float x, float y, float width, float height)
{
	if (raisedMask.empty())
		return;

	ofPixels px;
	px.allocate(cols, rows, OF_IMAGE_GRAYSCALE);
	memcpy(px.getData(), raisedMask.data, (size_t)cols * rows);

	ofTexture tex;
	tex.loadData(px);

	ofPushStyle();
	ofSetColor(255, 200, 0, 120);
	tex.draw(x, y, width, height);

	if (candidateActive) {
		float mx = x + ((candidateLocation.x - kinectROI.x) / kinectROI.width) * width;
		float my = y + ((candidateLocation.y - kinectROI.y) / kinectROI.height) * height;
		ofSetColor(confirmed ? ofColor(0, 255, 0) : ofColor(255, 255, 0));
		ofNoFill();
		ofSetLineWidth(2.0);
		ofDrawCircle(mx, my, 8);
	}
	ofPopStyle();
}
