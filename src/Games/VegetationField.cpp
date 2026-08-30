#include "VegetationField.h"
#include "ofxImGui.h"

float VegetationField::TEMPERATURE = 0.0f;
float VegetationField::WATER_LEVEL_BASE = -25.0f;
float VegetationField::SNOW_LEVEL_BASE = 60.0f;
float VegetationField::SHRUB_MIN_ABOVE_WATER = 8.0f;
float VegetationField::FRUIT_MIN_ABOVE_WATER = 25.0f;
float VegetationField::FRUIT_MAX_BELOW_SNOW = 25.0f;
float VegetationField::NUT_MIN_ABOVE_WATER = 11.0f;
float VegetationField::NUT_MAX_BELOW_SNOW = 11.0f;
float VegetationField::GROWTH_RATE = 0.6f;
float VegetationField::DECAY_RATE = 0.3f;

namespace {
	const int GRID_STEP = 4; // kinect pixels per cell, matching HandField/PuckTracker/MyceliumNetwork's convention
}

void VegetationField::setup(std::shared_ptr<KinectProjector> const& k)
{
	kinectProjector = k;
	step = GRID_STEP;
	cols = 0;
	rows = 0;
	gridReady = false;
}

void VegetationField::setKinectROI(ofRectangle & KROI)
{
	kinectROI = KROI;
	if (kinectROI.width <= 0 || kinectROI.height <= 0)
		return;

	int newCols = std::max(1, (int)(kinectROI.width / step));
	int newRows = std::max(1, (int)(kinectROI.height / step));
	if (newCols == cols && newRows == rows && gridReady)
		return; // grid dimensions unchanged - keep current vegetation cover as it is

	cols = newCols;
	rows = newRows;
	shrubDensity = cv::Mat::zeros(rows, cols, CV_32F);
	fruitDensity = cv::Mat::zeros(rows, cols, CV_32F);
	nutDensity = cv::Mat::zeros(rows, cols, CV_32F);
	gridReady = true;
}

void VegetationField::update()
{
	if (!kinectProjector->isImageStabilized() || kinectROI.width <= 0 || !gridReady)
		return;

	// See the header note: raising TEMPERATURE shifts both lines up
	// together, which floods more low ground AND shrinks the snowcap
	// (fewer cells clear the now-higher snow threshold) - exactly ELF's
	// 'q'/'a' behavior.
	float waterLevel = WATER_LEVEL_BASE + TEMPERATURE;
	float snowLevel = SNOW_LEVEL_BASE + TEMPERATURE;
	float dt = ofGetLastFrameTime();

	ofPixels px;
	px.allocate(cols, rows, OF_IMAGE_COLOR_ALPHA);
	unsigned char * data = px.getData();

	for (int gy = 0; gy < rows; gy++) {
		for (int gx = 0; gx < cols; gx++) {
			float kx = kinectROI.x + gx * step + step / 2.0f;
			float ky = kinectROI.y + gy * step + step / 2.0f;
			float elevation = kinectProjector->elevationAtKinectCoord(kx, ky);

			bool isWater = elevation < waterLevel;
			bool isSnow = !isWater && elevation > snowLevel;

			float & shrub = shrubDensity.at<float>(gy, gx);
			float & fruit = fruitDensity.at<float>(gy, gx);
			float & nut = nutDensity.at<float>(gy, gx);

			if (isWater || isSnow) {
				// Mirrors BDlocation.makeSnow()/makeWater() zeroing out
				// shrubs/fruits/nuts, just eased rather than instant.
				shrub = std::max(0.0f, shrub - DECAY_RATE * dt);
				fruit = std::max(0.0f, fruit - DECAY_RATE * dt);
				nut = std::max(0.0f, nut - DECAY_RATE * dt);
			} else {
				bool inShrubBand = elevation > waterLevel + SHRUB_MIN_ABOVE_WATER;
				bool inFruitBand = elevation > waterLevel + FRUIT_MIN_ABOVE_WATER && elevation < snowLevel - FRUIT_MAX_BELOW_SNOW;
				bool inNutBand = elevation > waterLevel + NUT_MIN_ABOVE_WATER && elevation < snowLevel - NUT_MAX_BELOW_SNOW;

				shrub = ofClamp(shrub + (inShrubBand ? GROWTH_RATE : -DECAY_RATE) * dt, 0.0f, 1.0f);
				fruit = ofClamp(fruit + (inFruitBand ? GROWTH_RATE : -DECAY_RATE) * dt, 0.0f, 1.0f);
				nut = ofClamp(nut + (inNutBand ? GROWTH_RATE : -DECAY_RATE) * dt, 0.0f, 1.0f);
			}

			int idx = (gy * cols + gx) * 4;
			data[idx + 0] = (unsigned char)(shrub * 255.0f);
			data[idx + 1] = (unsigned char)(fruit * 255.0f);
			data[idx + 2] = (unsigned char)(nut * 255.0f);
			data[idx + 3] = isWater ? 255 : (isSnow ? 128 : 0);
		}
	}

	combinedTex.loadData(px);
	// Nearest-neighbor, not the usual smooth interpolation - the blocky
	// per-cell patches are the point (see the header/paper reference on
	// ELF's own cell-rect rendering), softened only by the shader's own
	// noise-based edge jitter rather than by texture filtering.
	combinedTex.setTextureMinMagFilter(GL_NEAREST, GL_NEAREST);
}

void VegetationField::drawGui()
{
	ImGui::Begin("Vegetation");
	ImGui::Text("ELF-style flora: three plant types growing within elevation");
	ImGui::Text("bands relative to the water line - reshape the sand to see it shift.");
	ImGui::SliderFloat("Temperature", &TEMPERATURE, -60.0f, 60.0f);
	ImGui::Text("Higher temperature floods more land and shrinks the snowcap.");
	ImGui::SliderFloat("Water level (mm)", &WATER_LEVEL_BASE, -100.0f, 50.0f);
	ImGui::SliderFloat("Snow level (mm)", &SNOW_LEVEL_BASE, 0.0f, 150.0f);
	ImGui::Separator();
	ImGui::SliderFloat("Shrub min above water (mm)", &SHRUB_MIN_ABOVE_WATER, 0.0f, 50.0f);
	ImGui::SliderFloat("Fruit min above water (mm)", &FRUIT_MIN_ABOVE_WATER, 0.0f, 60.0f);
	ImGui::SliderFloat("Fruit max below snow (mm)", &FRUIT_MAX_BELOW_SNOW, 0.0f, 60.0f);
	ImGui::SliderFloat("Nut min above water (mm)", &NUT_MIN_ABOVE_WATER, 0.0f, 60.0f);
	ImGui::SliderFloat("Nut max below snow (mm)", &NUT_MAX_BELOW_SNOW, 0.0f, 60.0f);
	ImGui::Separator();
	ImGui::SliderFloat("Growth rate", &GROWTH_RATE, 0.0f, 2.0f);
	ImGui::SliderFloat("Decay rate", &DECAY_RATE, 0.0f, 2.0f);
	ImGui::End();
}
