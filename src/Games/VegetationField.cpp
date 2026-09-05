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
// 1:3:2 ratio, matching ELF's SHRUBGROWTH=1/FRUITGROWTH=3/NUTGROWTH=2.
float VegetationField::SHRUB_GROWTH_RATE = 0.2f;
float VegetationField::FRUIT_GROWTH_RATE = 0.6f;
float VegetationField::NUT_GROWTH_RATE = 0.4f;
float VegetationField::FOOD_PER_FULL_CELL = 255.0f;

namespace {
	// One cell per kinect pixel - see the header note on why this doesn't
	// share HandField/PuckTracker/MyceliumNetwork's GRID_STEP==4 convention.
	const int GRID_STEP = 1;
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
				// BDlocation.makeSnow()/makeWater() zero shrubs/fruits/nuts
				// outright, with no easing - do the same here.
				shrub = fruit = nut = 0.0f;
			} else {
				// One-way growth: a channel rises only while its band
				// condition holds; otherwise it just holds its current
				// value rather than decaying (see the header note - ELF's
				// stepCells() has no code path that shrinks these while
				// still land).
				bool inShrubBand = elevation > waterLevel + SHRUB_MIN_ABOVE_WATER;
				bool inFruitBand = elevation > waterLevel + FRUIT_MIN_ABOVE_WATER && elevation < snowLevel - FRUIT_MAX_BELOW_SNOW;
				bool inNutBand = elevation > waterLevel + NUT_MIN_ABOVE_WATER && elevation < snowLevel - NUT_MAX_BELOW_SNOW;

				if (inShrubBand) shrub = std::min(1.0f, shrub + SHRUB_GROWTH_RATE * dt);
				if (inFruitBand) fruit = std::min(1.0f, fruit + FRUIT_GROWTH_RATE * dt);
				if (inNutBand) nut = std::min(1.0f, nut + NUT_GROWTH_RATE * dt);
			}

			int idx = (gy * cols + gx) * 4;
			data[idx + 0] = (unsigned char)(shrub * 255.0f);
			data[idx + 1] = (unsigned char)(fruit * 255.0f);
			data[idx + 2] = (unsigned char)(nut * 255.0f);
			data[idx + 3] = isWater ? 255 : (isSnow ? 128 : 0);
		}
	}

	combinedTex.loadData(px);
	// Nearest-neighbor: at GRID_STEP==1 this is a no-op for alignment
	// (one texel per mesh pixel already), but keeps the shader's texel
	// lookup exact rather than blurring across the winner-take-all color
	// boundaries computed there.
	combinedTex.setTextureMinMagFilter(GL_NEAREST, GL_NEAREST);
}

bool VegetationField::cellIndexAt(float kx, float ky, int & gx, int & gy) const
{
	if (!gridReady)
		return false;
	gx = (int)((kx - kinectROI.x) / step);
	gy = (int)((ky - kinectROI.y) / step);
	return gx >= 0 && gx < cols && gy >= 0 && gy < rows;
}

float VegetationField::eatShrubOrFruit(float kx, float ky)
{
	int gx, gy;
	if (!cellIndexAt(kx, ky, gx, gy))
		return 0.0f;

	float & shrub = shrubDensity.at<float>(gy, gx);
	float & fruit = fruitDensity.at<float>(gy, gx);
	if (shrub > 0.0f) {
		float gained = shrub * FOOD_PER_FULL_CELL;
		shrub = 0.0f;
		return gained;
	}
	if (fruit > 0.0f) {
		float gained = fruit * FOOD_PER_FULL_CELL;
		fruit = 0.0f;
		return gained;
	}
	return 0.0f;
}

float VegetationField::eatFruitOrNut(float kx, float ky, bool preferNut, bool & ateNut)
{
	int gx, gy;
	if (!cellIndexAt(kx, ky, gx, gy))
		return 0.0f;

	float & fruit = fruitDensity.at<float>(gy, gx);
	float & nut = nutDensity.at<float>(gy, gx);
	float & first = preferNut ? nut : fruit;
	float & second = preferNut ? fruit : nut;
	if (first > 0.0f) {
		float gained = first * FOOD_PER_FULL_CELL;
		first = 0.0f;
		ateNut = preferNut;
		return gained;
	}
	if (second > 0.0f) {
		float gained = second * FOOD_PER_FULL_CELL;
		second = 0.0f;
		ateNut = !preferNut;
		return gained;
	}
	return 0.0f;
}

bool VegetationField::isWaterAt(float kx, float ky) const
{
	if (!kinectProjector)
		return false;
	float elevation = kinectProjector->elevationAtKinectCoord(kx, ky);
	return elevation < (WATER_LEVEL_BASE + TEMPERATURE);
}

bool VegetationField::isSnowAt(float kx, float ky) const
{
	if (!kinectProjector)
		return false;
	float elevation = kinectProjector->elevationAtKinectCoord(kx, ky);
	return elevation >= (WATER_LEVEL_BASE + TEMPERATURE) && elevation > (SNOW_LEVEL_BASE + TEMPERATURE);
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
	ImGui::SliderFloat("Shrub growth rate", &SHRUB_GROWTH_RATE, 0.0f, 2.0f);
	ImGui::SliderFloat("Fruit growth rate", &FRUIT_GROWTH_RATE, 0.0f, 2.0f);
	ImGui::SliderFloat("Nut growth rate", &NUT_GROWTH_RATE, 0.0f, 2.0f);
	ImGui::End();
}
