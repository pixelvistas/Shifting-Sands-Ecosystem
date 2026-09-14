#include "VegetationField.h"
#include "ofxImGui.h"

// Fraction of the calibrated elevation range (see setElevationRange() and
// the header note), not millimeters. Starts equal to BASE_TEMPERATURE so
// there's no artificial startup transient easing up from 0.
float VegetationField::TEMPERATURE = 0.75f;
// NOT ELF's literal default (200/255): with LIVING_RANGE_FRACTION at
// ELF's own literal ~0.784, ANY value here leaves the water/snow lines
// so close to the calibrated floor/ceiling that ordinary hand-sculpting
// can't reach them - confirmed on real hardware, not just in theory (see
// LIVING_RANGE_FRACTION's own comment below for the fix). 0.75 pairs
// with LIVING_RANGE_FRACTION=0.5 to center the living zone with 25%
// margin on both ends - still needs on-site placement against the real
// box, but this is a much more usable starting point than ELF's own
// ratio for realistic sculpting.
float VegetationField::BASE_TEMPERATURE = 0.75f;
// First-guess scale, untested on real hardware - see the header note.
// Average per-cell elevation change during active sculpting is expected
// to be a small fraction of a millimeter per frame (only cells near a
// hand actually move, diluted across the whole grid), so this is set
// high enough that ordinary sculpting registers a visible climate
// response; retune once real activity levels are observed.
float VegetationField::ACTIVITY_TO_TEMPERATURE = 0.3f;
float VegetationField::MAX_TEMPERATURE_OFFSET = 0.1f;
float VegetationField::TEMPERATURE_EASE_RATE = 0.5f;
// Typical per-pixel Kinect depth noise is on the order of 1-2mm at rest;
// set with headroom above that so a still sandbox reliably reads as zero
// activity rather than chasing sensor jitter - see the header note.
float VegetationField::ACTIVITY_NOISE_FLOOR = 3.0f;
// BDenvironment's SHRUBLINE=20/FRUITLINE=60/NUTLINE=25, each divided by
// 255 - see the header note on why these are fractions and on
// FRUITLINE/NUTLINE's relative sizes. LIVING_RANGE_FRACTION is the one
// exception: ELF's literal LIVINGRANGE=200/255 (~0.78) leaves almost no
// margin for water/snow within any realistically-sized calibrated range
// (confirmed on real hardware: a hand-built mound never reached the
// snowline, a hand-dug pit never reached the waterline, at this ratio).
// Loosened to 0.5 - still clearly the dominant fraction (most of the
// range is livable land, matching ELF's intent), but leaves 25% margin
// on each end instead of ELF's ~11%, so ordinary sculpting can actually
// reach both lines. This is a deliberate departure from ELF's exact
// ratio, not a bug - see BASE_TEMPERATURE's comment for the pairing.
float VegetationField::LIVING_RANGE_FRACTION = 0.5f;
float VegetationField::SHRUB_LINE_FRACTION = 20.0f / 255.0f;
float VegetationField::FRUIT_LINE_FRACTION = 60.0f / 255.0f;
float VegetationField::NUT_LINE_FRACTION = 25.0f / 255.0f;
// 1:3:2 ratio, matching ELF's SHRUBGROWTH=1/FRUITGROWTH=3/NUTGROWTH=2.
float VegetationField::SHRUB_GROWTH_RATE = 0.2f;
float VegetationField::FRUIT_GROWTH_RATE = 0.6f;
float VegetationField::NUT_GROWTH_RATE = 0.4f;
float VegetationField::FOOD_PER_FULL_CELL = 255.0f;

namespace {
	// One cell per kinect pixel - see the header note on matching ELF's
	// own native-resolution sampling.
	const int GRID_STEP = 1;
}

void VegetationField::setup(std::shared_ptr<KinectProjector> const& k)
{
	kinectProjector = k;
	step = GRID_STEP;
	cols = 0;
	rows = 0;
	gridReady = false;
	activityBaselineReady = false;
	activityLevel = 0.0f;
	// Sane fallback until setElevationRange() is called - keeps
	// normalizedElevation() well-defined even if update() somehow runs
	// first.
	elevationMin = -220.0f;
	elevationMax = 220.0f;
}

void VegetationField::setElevationRange(float minMM, float maxMM)
{
	elevationMin = minMM;
	elevationMax = maxMM;
}

float VegetationField::normalizedElevation(float elevationMM) const
{
	// SandSurfaceRenderer's elevationMin/elevationMax are NOT guaranteed
	// ascending - its own setup() deliberately sets elevationMin = higher
	// number, elevationMax = lower number (both negated from the loaded
	// colormap's height range) to feed a correspondingly negative
	// heightMapScale in its own affine formula. That convention is
	// self-consistent there, but this is a plain linear normalization, so
	// reorder by actual value here rather than trusting the field names.
	float lo = std::min(elevationMin, elevationMax);
	float hi = std::max(elevationMin, elevationMax);
	float range = hi - lo;
	if (range < 1.0f)
		range = 1.0f; // guard against a degenerate/uncalibrated range
	return ofClamp((elevationMM - lo) / range, 0.0f, 1.0f);
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
	previousElevation = cv::Mat::zeros(rows, cols, CV_32F);
	// A regenerated grid has no valid "last frame" to diff against yet -
	// update() seeds previousElevation on its first pass and skips the
	// activity computation that frame rather than reading a fake spike
	// off the freshly-zeroed matrix.
	activityBaselineReady = false;
	gridReady = true;
}

void VegetationField::update()
{
	if (!kinectProjector->isImageStabilized() || kinectROI.width <= 0 || !gridReady)
		return;

	// See the header note: raising TEMPERATURE shifts both lines up
	// together, which floods more low ground AND shrinks the snowcap
	// (fewer cells clear the now-higher snow threshold) - exactly ELF's
	// 'q'/'a' behavior. Both are fractions of the calibrated elevation
	// range, matching BDenvironment.stepCells()'s temperature/
	// temperature-LIVINGRANGE comparisons on its own normalized cellheight.
	float snowLevelFrac = TEMPERATURE;
	float waterLevelFrac = TEMPERATURE - LIVING_RANGE_FRACTION;
	float dt = ofGetLastFrameTime();

	ofPixels px;
	px.allocate(cols, rows, OF_IMAGE_COLOR_ALPHA);
	unsigned char * data = px.getData();

	// Participant-facing climate: TEMPERATURE is not a direct control (see
	// the header note) - it is driven by how much the sand is actively
	// being reshaped, accumulated below alongside the existing per-cell
	// elevation sampling so this costs nothing extra. Activity itself
	// stays in raw mm (compared against ACTIVITY_NOISE_FLOOR, also mm)
	// since it's a delta between two raw sensor readings, not a
	// classification decision.
	float activitySum = 0.0f;

	for (int gy = 0; gy < rows; gy++) {
		for (int gx = 0; gx < cols; gx++) {
			float kx = kinectROI.x + gx * step + step / 2.0f;
			float ky = kinectROI.y + gy * step + step / 2.0f;
			float elevation = kinectProjector->elevationAtKinectCoord(kx, ky);

			float & prevElevation = previousElevation.at<float>(gy, gx);
			if (activityBaselineReady) {
				float delta = std::abs(elevation - prevElevation);
				// Drop noise-floor-level deltas entirely rather than
				// damping them, so a still sandbox reads as exactly
				// zero activity - see the header note.
				if (delta > ACTIVITY_NOISE_FLOOR)
					activitySum += delta;
			}
			prevElevation = elevation;

			float elevFrac = normalizedElevation(elevation);
			bool isWater = elevFrac < waterLevelFrac;
			bool isSnow = !isWater && elevFrac > snowLevelFrac;

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
				// still land). Shrub has no upper bound, matching
				// stepCells()'s shrub check having no "< temperature - X"
				// clause the way fruit/nut's do.
				bool inShrubBand = elevFrac > waterLevelFrac + SHRUB_LINE_FRACTION;
				bool inFruitBand = elevFrac > waterLevelFrac + FRUIT_LINE_FRACTION && elevFrac < snowLevelFrac - FRUIT_LINE_FRACTION;
				bool inNutBand = elevFrac > waterLevelFrac + NUT_LINE_FRACTION && elevFrac < snowLevelFrac - NUT_LINE_FRACTION;

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

	if (activityBaselineReady) {
		activityLevel = activitySum / (float)(cols * rows);
		float offset = ofClamp(activityLevel * ACTIVITY_TO_TEMPERATURE, 0.0f, MAX_TEMPERATURE_OFFSET);
		float target = BASE_TEMPERATURE + offset;
		// Ease toward the target rather than snapping, so sustained
		// sculpting reads as gradual warming/cooling instead of chasing
		// every frame's sensor noise.
		TEMPERATURE += (target - TEMPERATURE) * std::min(1.0f, TEMPERATURE_EASE_RATE * dt);
	} else {
		activityBaselineReady = true;
	}
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
	float elevFrac = normalizedElevation(kinectProjector->elevationAtKinectCoord(kx, ky));
	return elevFrac < (TEMPERATURE - LIVING_RANGE_FRACTION);
}

bool VegetationField::isSnowAt(float kx, float ky) const
{
	if (!kinectProjector)
		return false;
	float elevFrac = normalizedElevation(kinectProjector->elevationAtKinectCoord(kx, ky));
	return elevFrac >= (TEMPERATURE - LIVING_RANGE_FRACTION) && elevFrac > TEMPERATURE;
}

void VegetationField::drawGui()
{
	ImGui::Begin("Vegetation");
	ImGui::Text("ELF-style flora: three plant types growing within elevation");
	ImGui::Text("bands relative to the water line - reshape the sand to see it shift.");
	ImGui::Separator();
	ImGui::Text("Climate (participant-driven, not a direct control)");
	ImGui::Text("Temperature: %.3f  (activity: %.3f mm/cell)", TEMPERATURE, activityLevel);
	// Reorder by actual value, same as normalizedElevation() - elevationMin
	// is NOT guaranteed the smaller of the two, see setElevationRange()'s
	// header note. This readout was inverted/wrong until now: it computed
	// straight from elevationMax-elevationMin without that reordering.
	float lo = std::min(elevationMin, elevationMax);
	float hi = std::max(elevationMin, elevationMax);
	float waterLevelFrac = TEMPERATURE - LIVING_RANGE_FRACTION;
	float waterLevelMM = lo + waterLevelFrac * (hi - lo);
	float snowLevelMM = lo + TEMPERATURE * (hi - lo);
	ImGui::Text("Water line: %.1f mm   Snow line: %.1f mm", waterLevelMM, snowLevelMM);
	ImGui::Text("Sustained sculpting raises it - floods more land, shrinks the snowcap.");
	ImGui::SliderFloat("Base temperature", &BASE_TEMPERATURE, 0.0f, 1.0f);
	ImGui::SliderFloat("Activity -> temperature scale", &ACTIVITY_TO_TEMPERATURE, 0.0f, 2.0f);
	ImGui::SliderFloat("Max activity offset", &MAX_TEMPERATURE_OFFSET, 0.0f, 0.5f);
	ImGui::SliderFloat("Temperature ease rate", &TEMPERATURE_EASE_RATE, 0.05f, 3.0f);
	ImGui::SliderFloat("Activity noise floor (mm)", &ACTIVITY_NOISE_FLOOR, 0.0f, 20.0f);
	ImGui::Separator();
	ImGui::Text("Calibrated elevation range (mm) - what water/snow lines above are");
	ImGui::Text("a fraction OF. Narrow this to your box's real achievable relief:");
	ImGui::Text("too wide and the whole 'living' zone swallows ordinary sculpting,");
	ImGui::Text("so nothing you build ever crosses it.");
	ImGui::Text("Order doesn't matter - normalizedElevation() sorts by value.");
	if (kinectProjector) {
		float ceiling = kinectProjector->getCalibratedCeilingElevation();
		ImGui::Text("Magic Sand's own calibrated ceiling: %.1f mm above the base plane", ceiling);
		ImGui::Text("(real per-installation measurement; no floor is calibrated, so the");
		ImGui::Text("floor default below just mirrors it - measure a real dig if you can)");
		if (ImGui::Button("Reset range to +-calibrated ceiling")) {
			elevationMin = -ceiling;
			elevationMax = ceiling;
		}
	}
	ImGui::SliderFloat("Elevation range bound A (mm)", &elevationMin, -400.0f, 400.0f);
	ImGui::SliderFloat("Elevation range bound B (mm)", &elevationMax, -400.0f, 400.0f);
	ImGui::Separator();
	ImGui::Text("ELF ratios (BDenvironment.LIVINGRANGE/SHRUBLINE/FRUITLINE/NUTLINE / 255)");
	ImGui::SliderFloat("Living range fraction", &LIVING_RANGE_FRACTION, 0.0f, 1.0f);
	ImGui::SliderFloat("Shrub line fraction", &SHRUB_LINE_FRACTION, 0.0f, 0.5f);
	ImGui::SliderFloat("Fruit line fraction", &FRUIT_LINE_FRACTION, 0.0f, 0.5f);
	ImGui::SliderFloat("Nut line fraction", &NUT_LINE_FRACTION, 0.0f, 0.5f);
	ImGui::Separator();
	ImGui::SliderFloat("Shrub growth rate", &SHRUB_GROWTH_RATE, 0.0f, 2.0f);
	ImGui::SliderFloat("Fruit growth rate", &FRUIT_GROWTH_RATE, 0.0f, 2.0f);
	ImGui::SliderFloat("Nut growth rate", &NUT_GROWTH_RATE, 0.0f, 2.0f);
	ImGui::End();
}
