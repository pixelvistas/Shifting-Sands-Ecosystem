#include "HandField.h"

float HandField::THRESHOLD = 20.0f; // raw depth units
float HandField::INFLUENCE_RADIUS = 80.0f; // kinect pixels
float HandField::STILL_THRESHOLD = 1.5f; // kinect pixels/frame
float HandField::VELOCITY_SMOOTHING = 0.25f;

namespace {
	const int GRID_STEP = 4; // kinect pixels per grid cell
}

void HandField::setup(std::shared_ptr<KinectProjector> const& k)
{
	kinectProjector = k;
	step = GRID_STEP;
	cols = 0;
	rows = 0;
	handVelocity = ofVec2f(0);
	hadHandLastFrame = false;
}

void HandField::update()
{
	kinectROI = kinectProjector->getKinectROI();
	ofVec2f kinectRes = kinectProjector->getKinectRes();
	int kw = (int)kinectRes.x;
	int kh = (int)kinectRes.y;

	if (kinectROI.width <= 0 || kinectROI.height <= 0 || kw <= 0 || kh <= 0) {
		handVelocity = ofVec2f(0);
		hadHandLastFrame = false;
		return;
	}

	const ofShortPixels & raw = kinectProjector->getRawDepthPixels();
	const ofFloatPixels & filtered = kinectProjector->getFilteredDepthPixels();
	if (raw.getWidth() != kw || raw.getHeight() != kh ||
		filtered.getWidth() != kw || filtered.getHeight() != kh) {
		handVelocity = ofVec2f(0);
		hadHandLastFrame = false;
		return; // buffers not ready yet
	}

	cols = std::max(1, (int)(kinectROI.width / step));
	rows = std::max(1, (int)(kinectROI.height / step));

	mask = cv::Mat::zeros(rows, cols, CV_8UC1);

	const unsigned short * rawData = raw.getData();
	const float * filteredData = filtered.getData();

	for (int gy = 0; gy < rows; gy++) {
		for (int gx = 0; gx < cols; gx++) {
			int px = (int)kinectROI.x + gx * step + step / 2;
			int py = (int)kinectROI.y + gy * step + step / 2;
			if (px < 0 || px >= kw || py < 0 || py >= kh)
				continue;

			int idx = py * kw + px;
			float rawDepth = rawData[idx];
			float filteredDepth = filteredData[idx];

			bool valid = rawDepth > 0 && rawDepth < 2047 && filteredDepth > 0 && filteredDepth < 4000;
			if (valid && (filteredDepth - rawDepth) > THRESHOLD) {
				mask.at<unsigned char>(gy, gx) = 255;
			}
		}
	}

	cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
	cv::erode(mask, mask, kernel, cv::Point(-1, -1), 1);
	cv::dilate(mask, mask, kernel, cv::Point(-1, -1), 2);

	cv::distanceTransform(mask, distField, cv::DIST_L2, 3);

	cv::Mat inverted;
	cv::bitwise_not(mask, inverted);
	cv::distanceTransform(inverted, freeDistField, cv::DIST_L2, 3);

	cv::Moments m = cv::moments(mask, true);
	bool handPresent = m.m00 > 0;
	if (handPresent) {
		handCentroid = ofVec2f(kinectROI.x + (float)(m.m10 / m.m00) * step,
		                        kinectROI.y + (float)(m.m01 / m.m00) * step);
		if (hadHandLastFrame) {
			ofVec2f rawVelocity = handCentroid - prevHandCentroid;
			// Exponential smoothing: raw per-frame centroid deltas are
			// noisy enough that a genuinely still hand rarely reads as
			// exactly zero, which is what made isHandStill() unreliable.
			handVelocity += (rawVelocity - handVelocity) * VELOCITY_SMOOTHING;
		} else {
			handVelocity = ofVec2f(0);
		}
		prevHandCentroid = handCentroid;
	} else {
		handVelocity = ofVec2f(0);
	}
	hadHandLastFrame = handPresent;
}

bool HandField::gridCoordAt(float x, float y, int & gx, int & gy) const
{
	if (cols == 0 || rows == 0)
		return false;

	gx = (int)((x - kinectROI.x) / step);
	gy = (int)((y - kinectROI.y) / step);
	if (gx < 0 || gx >= cols || gy < 0 || gy >= rows)
		return false;
	return true;
}

bool HandField::isInHand(float x, float y) const
{
	int gx, gy;
	if (!gridCoordAt(x, y, gx, gy))
		return false;
	return mask.at<unsigned char>(gy, gx) > 0;
}

ofVec2f HandField::pushDirection(float x, float y) const
{
	int gx, gy;
	if (!gridCoordAt(x, y, gx, gy) || distField.empty())
		return ofVec2f(0);
	if (mask.at<unsigned char>(gy, gx) == 0)
		return ofVec2f(0);

	// Central-difference gradient of the distance field. Distance grows
	// toward the interior of the hand blob, so the negative gradient
	// points toward the nearest way out.
	int gxLo = std::max(gx - 1, 0), gxHi = std::min(gx + 1, cols - 1);
	int gyLo = std::max(gy - 1, 0), gyHi = std::min(gy + 1, rows - 1);

	float dx = distField.at<float>(gy, gxHi) - distField.at<float>(gy, gxLo);
	float dy = distField.at<float>(gyHi, gx) - distField.at<float>(gyLo, gx);

	return ofVec2f(-dx, -dy);
}

ofVec2f HandField::herdForce(float x, float y) const
{
	if (freeDistField.empty() || handVelocity.lengthSquared() < 0.0001f)
		return ofVec2f(0);

	int gx, gy;
	if (!gridCoordAt(x, y, gx, gy))
		return ofVec2f(0);

	float distPixels = freeDistField.at<float>(gy, gx) * step;
	if (distPixels > INFLUENCE_RADIUS)
		return ofVec2f(0);

	float falloff = 1.0f - (distPixels / INFLUENCE_RADIUS);
	return handVelocity.getNormalized() * falloff;
}

void HandField::draw(float x, float y, float width, float height)
{
	if (mask.empty())
		return;

	ofPixels px;
	px.allocate(cols, rows, OF_IMAGE_GRAYSCALE);
	memcpy(px.getData(), mask.data, cols * rows);

	ofTexture tex;
	tex.loadData(px);

	ofPushStyle();
	ofSetColor(255, 0, 0, 120);
	tex.draw(x, y, width, height);
	ofPopStyle();
}
