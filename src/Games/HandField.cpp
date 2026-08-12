#include "HandField.h"

float HandField::THRESHOLD = 20.0f; // raw depth units

namespace {
	const int GRID_STEP = 4; // kinect pixels per grid cell
}

void HandField::setup(std::shared_ptr<KinectProjector> const& k)
{
	kinectProjector = k;
	step = GRID_STEP;
	cols = 0;
	rows = 0;
}

void HandField::update()
{
	kinectROI = kinectProjector->getKinectROI();
	ofVec2f kinectRes = kinectProjector->getKinectRes();
	int kw = (int)kinectRes.x;
	int kh = (int)kinectRes.y;

	if (kinectROI.width <= 0 || kinectROI.height <= 0 || kw <= 0 || kh <= 0)
		return;

	const ofShortPixels & raw = kinectProjector->getRawDepthPixels();
	const ofFloatPixels & filtered = kinectProjector->getFilteredDepthPixels();
	if (raw.getWidth() != kw || raw.getHeight() != kh ||
		filtered.getWidth() != kw || filtered.getHeight() != kh)
		return; // buffers not ready yet

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
