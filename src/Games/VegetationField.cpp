#include "VegetationField.h"
#include "ofxImGui.h"

// Fraction of the calibrated elevation range (see setElevationRange() and
// the header note), not millimeters. Starts equal to BASE_TEMPERATURE so
// there's no artificial startup transient easing up from 0.
//
// NOT literally 200/255 (ELF's factory-default `temperature` field) -
// see BASE_TEMPERATURE's comment below for why that exact value makes
// water mathematically unreachable, confirmed directly from
// BDenvironment.stepCells()/incTemp(), and why this starts above
// LIVING_RANGE_FRACTION instead, matching what an ELF operator is
// expected to do at session start (2026-09-24).
float VegetationField::TEMPERATURE = 0.9f;
// ELF's own factory-default `temperature` field is literally 200/255,
// the SAME value as LIVINGRANGE's 200/255 - which makes
// BDenvironment.stepCells()'s water condition (`height < temperature -
// LIVINGRANGE`) exactly `height < 0`, mathematically impossible (height
// can't go negative). Confirmed this isn't an oversight: incTemp()/
// decTemp() (ELF's 'q'/'a' operator keys) exist specifically so an
// operator raises temperature above LIVINGRANGE during a session to
// open a real water band - ELF ships in a deliberate zero-water cold
// start, not a bug. A real ELF reference figure the user provided
// (captioned "ELF Dynamic System with HIGHER TEMPERATURES... areas
// under water") confirms this directly: substantial visible water there
// required an operator-raised temperature, exactly this mechanism.
//
// So starting at ELF's literal 200/255 here would reproduce that same
// zero-water cold start, not a usable default - this uses ELF's own
// lever instead (temperature raised above LIVING_RANGE_FRACTION) rather
// than inventing a new one. With LIVING_RANGE_FRACTION at ELF's literal
// ~0.784 (see that field's comment), only 1-0.784=~0.216 of the range is
// left as slack, split between water headroom (how far TEMPERATURE
// exceeds LIVING_RANGE_FRACTION) and snow headroom (how far it stays
// under 1.0) - the two draw from the same budget, so both can't be maxed
// at once. 0.9 splits that budget so ~12% of the range is reachable as
// water and ~10% as snow - both genuinely present, neither dominant, a
// first-guess starting point per the user's explicit goal ("water where
// it makes sense, snow where it makes sense") - untested on real
// hardware, retune live in the Vegetation panel from here.
float VegetationField::BASE_TEMPERATURE = 0.9f;
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
// ELF's literal LIVINGRANGE=200/255 (~0.78) - see BASE_TEMPERATURE's
// comment above: a previous session loosened this to 0.5 (paired with
// BASE_TEMPERATURE=0.75) after real hardware testing found this literal
// ratio leaves almost no margin for water/snow within any realistically-
// sized calibrated range (a hand-built mound never reached the snowline,
// a hand-dug pit never reached the waterline). That finding still
// stands. Reverted to ELF's literal value anyway per explicit user
// instruction (2026-09-24) - match ELF's defaults first, retune from
// there if needed. Live-tunable in the Vegetation panel regardless.
float VegetationField::LIVING_RANGE_FRACTION = 200.0f / 255.0f;
// BDenvironment's SHRUBLINE=20/FRUITLINE=60/NUTLINE=25 each divided by
// LIVINGRANGE=200 - i.e. fractions OF THE LIVING RANGE, not of the
// total calibrated range - matching ELF exactly regardless of what
// LIVING_RANGE_FRACTION itself is currently set to (ELF's own literal
// value again as of 2026-09-24, see that field's comment - but this
// fraction-of-living-range framing is what would keep these bands
// correctly proportioned even if it's retuned away from ELF's value
// again later; fixing these as fractions of the TOTAL range instead
// previously left fruit's band crushed to a near-hairline width when
// living range was loosened, confirmed on real hardware).
float VegetationField::SHRUB_LINE_RATIO = 20.0f / 200.0f;
float VegetationField::FRUIT_LINE_RATIO = 60.0f / 200.0f;
float VegetationField::NUT_LINE_RATIO = 25.0f / 200.0f;
// ELF's literal SHRUBGROWTH=1/FRUITGROWTH=3/NUTGROWTH=2 - percent chance
// per tick of a +1/255 density increment, not a per-second rate. See the
// header note on why this reverted from continuous growth.
float VegetationField::SHRUB_GROWTH_CHANCE_PCT = 1.0f;
float VegetationField::FRUIT_GROWTH_CHANCE_PCT = 3.0f;
float VegetationField::NUT_GROWTH_CHANCE_PCT = 2.0f;
// CA-style spread layer - see the header note. First-guess defaults,
// untested on real hardware: a same-species neighbor within 40mm gives
// 5x the spontaneous chance to grow. ENABLE_SPREAD defaults on; it's an
// A/B performance kill switch, not a feature the user should need to
// know about to use the panel normally.
bool VegetationField::ENABLE_SPREAD = true;
float VegetationField::SPREAD_RADIUS_MM = 40.0f;
float VegetationField::SPREAD_CHANCE_MULTIPLIER = 5.0f;
// New departure from ELF's literal getCellColor(), see the header note -
// 0.05 (~13/255, ~13 successful nut growth ticks) is a first-guess
// starting point: enough to filter out a single-tick "flash to black"
// while still committing well before nut's glacial multi-thousand-tick
// path to full density. Untested on real hardware for whether it looks
// right - live-tunable in the Vegetation panel.
float VegetationField::NUT_VISIBILITY_THRESHOLD = 0.05f;
float VegetationField::FOOD_PER_FULL_CELL = 255.0f;
bool VegetationField::DEBUG_SHOW_SNOW = false;

namespace {
	// One cell per kinect pixel - see the header note on matching ELF's
	// own native-resolution sampling.
	const int GRID_STEP = 1;
	// ELF's density scale is 0..255 (see BDlocation's shrubs/fruits/nuts
	// int fields, constrainVal(0,255)); this class's is 0..1, so ELF's
	// literal "+1" per successful growth tick is 1/255 here.
	const float GROWTH_INCREMENT = 1.0f / 255.0f;
	// Fixed sample count for hasEstablishedNeighbor()'s stochastic radius
	// check - not user-tunable, see that method's header comment on why
	// this is sampled rather than scanned exhaustively. Cut from an
	// initial 8 after a measured 30-60x real-hardware slowdown (60fps to
	// 1-2fps) - this alone isn't what fixed it (see SPREAD_CHECK_CHANCE
	// below for the bigger lever), but every sample here is a real cost.
	const int SPREAD_SAMPLE_COUNT = 3;
	// Only this fraction of a cell's growth-eligible ticks actually pay
	// for the (still nontrivial) neighbor search at all - the rest just
	// use the plain spontaneous chance, skipping hasEstablishedNeighbor()
	// entirely. This is the main fix for the 30-60x real-hardware
	// slowdown: cutting call volume beats cutting cost-per-call, and
	// growth unfolds over minutes regardless (see the header note), so a
	// cell doesn't need its neighbor re-checked every single frame for
	// the spread layer to still work.
	const float SPREAD_CHECK_CHANCE = 0.1f;
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
	// Sane fallback until setKinectROI() computes a real value - avoids a
	// degenerate zero-radius neighbor search if update() somehow runs first.
	mmPerCell = 1.0f;
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

bool VegetationField::hasEstablishedNeighbor(int gx, int gy, cv::Mat const& density) const
{
	// Raw pointer access, not density.at<float>() - .at() recomputes
	// strides and bounds-checks on every single call, which is fine at
	// grid-build-once cost but was a measured, severe (30-60x, 60fps to
	// 1-2fps on real hardware) regression at this call volume: up to
	// cols*rows*3 species*SPREAD_SAMPLE_COUNT calls every frame. density
	// is guaranteed continuous (freshly allocated via cv::Mat::zeros() in
	// setKinectROI(), never reshaped afterward), so one row-major pointer
	// is valid for the whole matrix.
	const float * data = density.ptr<float>(0);
	int cellRadius = std::max(1, (int)(SPREAD_RADIUS_MM / mmPerCell));
	for (int i = 0; i < SPREAD_SAMPLE_COUNT; i++) {
		// Uniform-within-a-disk sampling (sqrt on the radius fraction),
		// not a square - SPREAD_RADIUS_MM reads as a real circular
		// neighborhood, not a diamond/box one.
		float angle = ofRandom(TWO_PI);
		float r = std::sqrt(ofRandom(1.0f)) * cellRadius;
		int nx = gx + (int)(std::cos(angle) * r);
		int ny = gy + (int)(std::sin(angle) * r);
		if (nx < 0 || nx >= cols || ny < 0 || ny >= rows)
			continue;
		if (data[ny * cols + nx] > 0.0f)
			return true;
	}
	return false;
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

	// Real-world mm spanned by one grid step, measured via two adjacent
	// pixels' actual world coordinates rather than assumed - a Kinect
	// pixel's real-world footprint depends on the installation's distance/
	// calibration, not a fixed constant. See the header note on
	// SPREAD_RADIUS_MM for why this needs to be a real distance.
	if (kinectProjector) {
		ofVec3f p0 = kinectProjector->kinectCoordToWorldCoord(kinectROI.x, kinectROI.y);
		ofVec3f p1 = kinectProjector->kinectCoordToWorldCoord(kinectROI.x + step, kinectROI.y);
		float dx = p1.x - p0.x;
		float dy = p1.y - p0.y;
		mmPerCell = std::sqrt(dx * dx + dy * dy);
		if (mmPerCell < 0.01f)
			mmPerCell = 1.0f; // guard against a degenerate/uncalibrated reading
	}
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
	// Derived fresh each frame from the *_LINE_RATIO tunables (fractions
	// of LIVING_RANGE_FRACTION, not of the total range) so shrub/fruit/nut
	// band widths stay proportional to whatever the living range is
	// currently tuned to - see the header note and SHRUB_LINE_RATIO's own
	// comment.
	float shrubLineFrac = SHRUB_LINE_RATIO * LIVING_RANGE_FRACTION;
	float fruitLineFrac = FRUIT_LINE_RATIO * LIVING_RANGE_FRACTION;
	float nutLineFrac = NUT_LINE_RATIO * LIVING_RANGE_FRACTION;
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
				bool inShrubBand = elevFrac > waterLevelFrac + shrubLineFrac;
				bool inFruitBand = elevFrac > waterLevelFrac + fruitLineFrac && elevFrac < snowLevelFrac - fruitLineFrac;
				bool inNutBand = elevFrac > waterLevelFrac + nutLineFrac && elevFrac < snowLevelFrac - nutLineFrac;

				// BDlocation.growShrubs()/growFruits()/growNuts() literally:
				// a per-tick coin flip, not a continuous rate - one
				// update() call is one tick, same convention Critter/
				// HumanAgent already use. See the header note. Each species'
				// chance is boosted by SPREAD_CHANCE_MULTIPLIER when a
				// same-species neighbor is already established nearby -
				// otherwise it falls back to the plain spontaneous chance,
				// which is the only way anything grows at all until the
				// hydrology/seeding layer exists to plant the first seeds.
				// The neighbor search itself only runs on a small fraction
				// of ticks (SPREAD_CHECK_CHANCE) - see that constant's
				// comment for why this was necessary, not optional, after
				// a measured real-hardware regression.
				if (inShrubBand && shrub < 1.0f) {
					float chance = SHRUB_GROWTH_CHANCE_PCT;
					if (ENABLE_SPREAD && ofRandom(1.0f) < SPREAD_CHECK_CHANCE && hasEstablishedNeighbor(gx, gy, shrubDensity)) chance *= SPREAD_CHANCE_MULTIPLIER;
					if (ofRandom(100.0f) < chance) shrub = std::min(1.0f, shrub + GROWTH_INCREMENT);
				}
				if (inFruitBand && fruit < 1.0f) {
					float chance = FRUIT_GROWTH_CHANCE_PCT;
					if (ENABLE_SPREAD && ofRandom(1.0f) < SPREAD_CHECK_CHANCE && hasEstablishedNeighbor(gx, gy, fruitDensity)) chance *= SPREAD_CHANCE_MULTIPLIER;
					if (ofRandom(100.0f) < chance) fruit = std::min(1.0f, fruit + GROWTH_INCREMENT);
				}
				if (inNutBand && nut < 1.0f) {
					float chance = NUT_GROWTH_CHANCE_PCT;
					if (ENABLE_SPREAD && ofRandom(1.0f) < SPREAD_CHECK_CHANCE && hasEstablishedNeighbor(gx, gy, nutDensity)) chance *= SPREAD_CHANCE_MULTIPLIER;
					if (ofRandom(100.0f) < chance) nut = std::min(1.0f, nut + GROWTH_INCREMENT);
				}
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
	// Ground truth, independent of any threshold/classification below -
	// put a hand at the ROI center and read the real mm value directly,
	// to actually measure this box's achievable relief instead of
	// guessing at elevation range/temperature values blind. This is what
	// the calibrated-elevation-range sliders and Water/Snow line readout
	// further down should be checked against, not the other way around.
	//
	// Sampled ONCE here and reused by the pipeline diagnostic below,
	// rather than each querying elevationAtKinectCoord() independently -
	// two separate real-time queries in the same draw call could
	// legitimately disagree if the grabber thread writes a new depth
	// frame between them, which would make this line and the
	// classification below it describe two different elevations while
	// both claiming to be "the ROI center."
	bool haveRawElevation = false;
	float rawElevationAtROICenter = 0.0f;
	if (kinectProjector && kinectROI.width > 0) {
		float cx = kinectROI.x + kinectROI.width / 2.0f;
		float cy = kinectROI.y + kinectROI.height / 2.0f;
		rawElevationAtROICenter = kinectProjector->elevationAtKinectCoord(cx, cy);
		haveRawElevation = true;
		ImGui::Text("Raw elevation at ROI center: %.1f mm (put a hand/dig/mound here to measure)", rawElevationAtROICenter);
	}
	ImGui::Separator();
	// Direct visibility into update()'s early-return gate and the actual
	// per-cell growth computation at the one point we already have a raw
	// elevation reading for (ROI center) - added because "blank white,
	// even with zero agents and after several seconds" is consistent with
	// at least three different root causes (pipeline never running,
	// elevation never landing in a growth band, or growth computed but not
	// visibly accumulating) that look identical from a screenshot alone.
	if (kinectProjector) {
		bool stabilized = kinectProjector->isImageStabilized();
		bool roiValid = kinectROI.width > 0;
		ImGui::Text("Pipeline: stabilized=%s  ROI valid=%s  grid ready=%s (%d x %d cells)",
			stabilized ? "yes" : "NO", roiValid ? "yes" : "NO", gridReady ? "yes" : "NO", cols, rows);
		if (!stabilized || !roiValid || !gridReady) {
			ImGui::Text("*** update() is returning early right now - nothing below this line is running. ***");
		} else if (roiValid && haveRawElevation) {
			int gx, gy;
			float cx = kinectROI.x + kinectROI.width / 2.0f;
			float cy = kinectROI.y + kinectROI.height / 2.0f;
			if (cellIndexAt(cx, cy, gx, gy)) {
				// Mirrors update()'s classification exactly (isWater/isSnow
				// checked first, shrub has no upper bound) rather than a
				// re-derived approximation, so this can't disagree with
				// what update() actually computed for this same cell. Reuses
				// the SAME sample the "Raw elevation" line above printed,
				// rather than a second independent query that could race
				// against a grabber-thread depth update and silently
				// describe a different moment - see that line's comment.
				float elevFrac = normalizedElevation(rawElevationAtROICenter);
				float waterFrac = TEMPERATURE - LIVING_RANGE_FRACTION;
				float snowFrac = TEMPERATURE;
				bool isWater = elevFrac < waterFrac;
				bool isSnow = !isWater && elevFrac > snowFrac;
				float shrubLineFrac = SHRUB_LINE_RATIO * LIVING_RANGE_FRACTION;
				float fruitLineFrac = FRUIT_LINE_RATIO * LIVING_RANGE_FRACTION;
				float nutLineFrac = NUT_LINE_RATIO * LIVING_RANGE_FRACTION;
				bool inShrubBand = !isWater && !isSnow && elevFrac > waterFrac + shrubLineFrac;
				bool inFruitBand = !isWater && !isSnow && elevFrac > waterFrac + fruitLineFrac && elevFrac < snowFrac - fruitLineFrac;
				bool inNutBand = !isWater && !isSnow && elevFrac > waterFrac + nutLineFrac && elevFrac < snowFrac - nutLineFrac;
				ImGui::Text("ROI-center cell: water=%s snow=%s in-band [shrub=%s fruit=%s nut=%s]",
					isWater ? "yes" : "no", isSnow ? "yes" : "no",
					inShrubBand ? "yes" : "no", inFruitBand ? "yes" : "no", inNutBand ? "yes" : "no");
				ImGui::Text("ROI-center density: shrub=%.4f fruit=%.4f nut=%.4f",
					shrubDensity.at<float>(gy, gx), fruitDensity.at<float>(gy, gx), nutDensity.at<float>(gy, gx));
				ImGui::Text("(+1/255 only on a successful per-tick chance roll while in-band -");
				ImGui::Text("thousands of ticks to visibly accumulate is expected, not a bug)");
			}
		}
	}
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
	ImGui::Checkbox("Debug: render snow as pale blue (not flat white)", &DEBUG_SHOW_SNOW);
	ImGui::Text("Snow normally looks identical to un-grown land - both flat white -");
	ImGui::Text("so this is the only way to tell by eye whether a blank area is");
	ImGui::Text("'still growing' or 'already snow' while diagnosing calibration.");
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
	ImGui::Text("Shrub/fruit/nut lines below are fractions OF the living range above,");
	ImGui::Text("not of the total range - so band widths stay proportional if you");
	ImGui::Text("retune the living range fraction. See the header note.");
	ImGui::SliderFloat("Shrub line ratio (of living range)", &SHRUB_LINE_RATIO, 0.0f, 0.5f);
	ImGui::SliderFloat("Fruit line ratio (of living range)", &FRUIT_LINE_RATIO, 0.0f, 0.5f);
	ImGui::SliderFloat("Nut line ratio (of living range)", &NUT_LINE_RATIO, 0.0f, 0.5f);
	{
		float shrubLineFrac = SHRUB_LINE_RATIO * LIVING_RANGE_FRACTION;
		float fruitLineFrac = FRUIT_LINE_RATIO * LIVING_RANGE_FRACTION;
		float nutLineFrac = NUT_LINE_RATIO * LIVING_RANGE_FRACTION;
		float rangeMM = hi - lo;
		float shrubWidthMM = std::max(0.0f, LIVING_RANGE_FRACTION - shrubLineFrac) * rangeMM;
		float fruitWidthMM = std::max(0.0f, LIVING_RANGE_FRACTION - 2.0f * fruitLineFrac) * rangeMM;
		float nutWidthMM = std::max(0.0f, LIVING_RANGE_FRACTION - 2.0f * nutLineFrac) * rangeMM;
		ImGui::Text("Resulting band widths: shrub %.1f mm, fruit %.1f mm, nut %.1f mm", shrubWidthMM, fruitWidthMM, nutWidthMM);
	}
	ImGui::Separator();
	ImGui::Text("Growth: ELF's literal per-tick coin flip (Math.random()*100 <");
	ImGui::Text("chance), not a continuous rate - a %% chance each frame of +1/255");
	ImGui::Text("density. Reaching full density takes thousands of in-band ticks,");
	ImGui::Text("same glacial pace as real ELF, not the few-second fill the earlier");
	ImGui::Text("continuous version had.");
	ImGui::SliderFloat("Shrub growth chance (% per tick)", &SHRUB_GROWTH_CHANCE_PCT, 0.0f, 20.0f);
	ImGui::SliderFloat("Fruit growth chance (% per tick)", &FRUIT_GROWTH_CHANCE_PCT, 0.0f, 20.0f);
	ImGui::SliderFloat("Nut growth chance (% per tick)", &NUT_GROWTH_CHANCE_PCT, 0.0f, 20.0f);
	ImGui::Separator();
	ImGui::Text("Spread (succession-model phase, layered on top of ELF's growth");
	ImGui::Text("above): a same-species neighbor within this real-world radius");
	ImGui::Text("multiplies that species' own growth chance for this cell - the");
	ImGui::Text("1:3:2 shrub:fruit:nut ratio above carries through unchanged, since");
	ImGui::Text("all three are scaled by the same multiplier. With no established");
	ImGui::Text("neighbor nearby, growth still falls back to the plain spontaneous");
	ImGui::Text("chance above - the only way anything grows before the hydrology/");
	ImGui::Text("seeding layer exists to plant a first seed.");
	ImGui::Checkbox("Enable spread (uncheck to A/B test performance)", &ENABLE_SPREAD);
	ImGui::Text("If frame rate jumps back up with this off, spread is still the");
	ImGui::Text("bottleneck - if not, something else is. No rebuild needed to check.");
	ImGui::SliderFloat("Spread radius (mm)", &SPREAD_RADIUS_MM, 0.0f, 200.0f);
	ImGui::SliderFloat("Spread chance multiplier", &SPREAD_CHANCE_MULTIPLIER, 1.0f, 20.0f);
	ImGui::Text("mm per grid cell (measured): %.2f -> spread radius is ~%d cells", mmPerCell, std::max(1, (int)(SPREAD_RADIUS_MM / mmPerCell)));
	ImGui::Separator();
	ImGui::Text("Nut visibility threshold (NOT in ELF): nut's color, (0,h,h),");
	ImGui::Text("has no channel pinned bright like shrub/fruit do, so a single");
	ImGui::Text("lucky growth tick at low elevation renders solid near-black -");
	ImGui::Text("confirmed on real hardware as random black speckle scattered");
	ImGui::Text("through otherwise-unrelated land, not a tie/negative-space cell");
	ImGui::Text("and not a contour line. Below this density, nut stays negative");
	ImGui::Text("space (like a real tie) instead of committing to that color.");
	ImGui::SliderFloat("Nut visibility threshold", &NUT_VISIBILITY_THRESHOLD, 0.0f, 0.3f);
	ImGui::End();
}
