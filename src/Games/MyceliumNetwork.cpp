#include "MyceliumNetwork.h"
#include "ofxImGui.h"

float MyceliumNetwork::DIG_THRESHOLD = 15.0f;
float MyceliumNetwork::REVEAL_RISE_RATE = 1.5f;
float MyceliumNetwork::REVEAL_DECAY_RATE = 0.3f;
int MyceliumNetwork::NETWORK_SEED_COUNT = 8;
float MyceliumNetwork::NETWORK_BRANCH_CHANCE = 0.35f;
float MyceliumNetwork::NETWORK_MAX_SEGMENT_CELLS = 14.0f;
ofColor MyceliumNetwork::GLOW_COLOR = ofColor(190, 255, 210); // pale bioluminescent

namespace {
	const int GRID_STEP = 4; // kinect pixels per cell, matching HandField/PuckTracker's convention
}

void MyceliumNetwork::setup(std::shared_ptr<KinectProjector> const& k)
{
	kinectProjector = k;
	step = GRID_STEP;
	cols = 0;
	rows = 0;
	patternGenerated = false;
}

void MyceliumNetwork::setKinectROI(ofRectangle & KROI)
{
	kinectROI = KROI;
	if (kinectROI.width <= 0 || kinectROI.height <= 0)
		return;

	int newCols = std::max(1, (int)(kinectROI.width / step));
	int newRows = std::max(1, (int)(kinectROI.height / step));
	if (newCols == cols && newRows == rows && patternGenerated)
		return; // grid dimensions unchanged - keep the buried network and dig progress as they are

	cols = newCols;
	rows = newRows;
	revealedAccum = cv::Mat::zeros(rows, cols, CV_32F);
	regenerateNetworkPattern();
	patternGenerated = true;
}

void MyceliumNetwork::regenerateNetworkPattern()
{
	networkPattern = cv::Mat::zeros(rows, cols, CV_32F);

	for (int s = 0; s < NETWORK_SEED_COUNT; s++) {
		ofVec2f start(ofRandom((float)cols), ofRandom((float)rows));
		float angle = ofRandom(TWO_PI);
		growBranch(start, angle, NETWORK_MAX_SEGMENT_CELLS * ofRandom(1.5f, 3.0f), 0);
	}

	// Soften the raster lines into slightly fuzzy filaments rather than
	// razor-sharp single-pixel-wide threads.
	cv::GaussianBlur(networkPattern, networkPattern, cv::Size(3, 3), 0);
}

void MyceliumNetwork::growBranch(ofVec2f pos, float angle, float remainingLength, int depth)
{
	if (remainingLength <= 0.0f || depth > 6)
		return;

	float segmentLength = std::min(ofRandom(NETWORK_MAX_SEGMENT_CELLS * 0.3f, NETWORK_MAX_SEGMENT_CELLS), remainingLength);
	angle += ofRandom(-0.5f, 0.5f); // gentle organic wander rather than straight lines

	ofVec2f next = pos + ofVec2f(cos(angle), sin(angle)) * segmentLength;
	cv::line(networkPattern, cv::Point((int)pos.x, (int)pos.y), cv::Point((int)next.x, (int)next.y), cv::Scalar(1.0f), 1);

	if (ofRandom(1.0f) < NETWORK_BRANCH_CHANCE)
		growBranch(next, angle + ofRandom(0.6f, 1.4f), remainingLength - segmentLength, depth + 1);
	growBranch(next, angle + ofRandom(-0.6f, 0.6f), remainingLength - segmentLength, depth + 1);
}

void MyceliumNetwork::update()
{
	if (!kinectProjector->isImageStabilized() || kinectROI.width <= 0 || !patternGenerated)
		return;

	elevationGrid = cv::Mat(rows, cols, CV_32F);
	for (int gy = 0; gy < rows; gy++) {
		for (int gx = 0; gx < cols; gx++) {
			float kx = kinectROI.x + gx * step + step / 2.0f;
			float ky = kinectROI.y + gy * step + step / 2.0f;
			elevationGrid.at<float>(gy, gx) = kinectProjector->elevationAtKinectCoord(kx, ky);
		}
	}

	int blurCells = std::max(5, (int)NETWORK_MAX_SEGMENT_CELLS);
	if (blurCells % 2 == 0) blurCells += 1;
	cv::GaussianBlur(elevationGrid, blurredGrid, cv::Size(blurCells, blurCells), 0);

	// A cell is "dug" where the actual sand sits meaningfully below its
	// own local surroundings - the mirror image of PuckTracker's "raised"
	// detection, tuned for depressions instead of bumps.
	cv::Mat dug = (blurredGrid - elevationGrid) > DIG_THRESHOLD;

	float dt = ofGetLastFrameTime();
	for (int gy = 0; gy < rows; gy++) {
		for (int gx = 0; gx < cols; gx++) {
			float & revealed = revealedAccum.at<float>(gy, gx);
			bool isDug = dug.at<uchar>(gy, gx) != 0;
			if (isDug)
				revealed = std::min(1.0f, revealed + REVEAL_RISE_RATE * dt);
			else
				revealed = std::max(0.0f, revealed - REVEAL_DECAY_RATE * dt);
		}
	}

	cv::Mat combinedF = networkPattern.mul(revealedAccum);

	ofPixels px;
	px.allocate(cols, rows, OF_IMAGE_GRAYSCALE);
	unsigned char * data = px.getData();
	for (int gy = 0; gy < rows; gy++)
		for (int gx = 0; gx < cols; gx++)
			data[gy * cols + gx] = (unsigned char)ofClamp(combinedF.at<float>(gy, gx) * 255.0f, 0.0f, 255.0f);

	combinedTex.loadData(px);
}

void MyceliumNetwork::drawGui()
{
	ImGui::Begin("Mycelium");
	ImGui::Text("Dig into the sand to reveal the buried network.");
	ImGui::SliderFloat("Dig threshold (mm)", &DIG_THRESHOLD, 2.0f, 60.0f);
	ImGui::SliderFloat("Reveal rise rate", &REVEAL_RISE_RATE, 0.1f, 5.0f);
	ImGui::SliderFloat("Reveal decay rate", &REVEAL_DECAY_RATE, 0.0f, 5.0f);
	ImGui::SliderInt("Network seeds", &NETWORK_SEED_COUNT, 1, 20);
	ImGui::SliderFloat("Branch chance", &NETWORK_BRANCH_CHANCE, 0.0f, 0.9f);
	ImGui::SliderFloat("Max segment length (cells)", &NETWORK_MAX_SEGMENT_CELLS, 4.0f, 40.0f);
	if (ImGui::Button("Regenerate buried network"))
		regenerateNetworkPattern();
	ImGui::End();
}
