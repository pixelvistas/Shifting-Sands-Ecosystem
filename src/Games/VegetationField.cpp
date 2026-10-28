#include "VegetationField.h"
#include "ofxImGui.h"
#include <algorithm>
#include <cmath>

// Fraction of the calibrated elevation range (see setElevationRange() and
// the header note), not millimeters. Starts equal to BASE_TEMPERATURE so
// there's no artificial startup transient easing up from 0.
//
// NOT literally 200/255 (ELF's factory-default `temperature` field) -
// see BASE_TEMPERATURE's comment below for why that exact value makes
// water mathematically unreachable, confirmed directly from
// BDenvironment.stepCells()/incTemp().
float VegetationField::TEMPERATURE = 0.745f;
// ELF's own factory-default `temperature` field is literally 200/255,
// the SAME value as LIVINGRANGE's 200/255 - which makes
// BDenvironment.stepCells()'s water condition (`height < temperature -
// LIVINGRANGE`) exactly `height < 0`, mathematically impossible (height
// can't go negative). Confirmed this isn't an oversight: incTemp()/
// decTemp() (ELF's 'q'/'a' operator keys) exist specifically so an
// operator raises temperature above LIVINGRANGE during a session to
// open a real water band - ELF ships in a deliberate zero-water cold
// start, not a bug.
//
// RETUNED 2026-09-25 with real hardware measurements, superseding the
// 0.9 first-guess this was set to the day before. That 0.9 value paired
// with LIVING_RANGE_FRACTION at ELF's literal ~0.784 put the water line
// at -104.5mm on this box's calibrated range (-142.606..142.606mm) -
// confirmed by direct measurement that the user's own max hand-dig only
// reaches -41.8mm, and even TEMPERATURE=1.0 (the practical ceiling -
// anything higher makes snow literally unreachable) only gets water to
// -81mm. No amount of TEMPERATURE tuning alone could close that gap -
// LIVING_RANGE_FRACTION itself needed to move (see that field's
// comment). With the user's second real measurement (max hand-built
// mound reaches +80mm), both lines are now solved for directly from
// measured targets rather than guessed: water line -35mm (7mm margin
// under the -41.8mm dig limit), snow line +70mm (10mm margin under the
// +80mm mound limit). Solving snowLevelFrac=TEMPERATURE for a snow line
// of 70mm on this range gives 0.745.
float VegetationField::BASE_TEMPERATURE = 0.745f;
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
// ELF's literal LIVINGRANGE=200/255 (~0.78) departed from again on
// 2026-09-25, per explicit user confirmation, after hardware measurement
// (not just the general finding from before) confirmed it impractical
// on this box - see BASE_TEMPERATURE's comment for the exact numbers
// (max hand-dig -41.8mm couldn't reach even a TEMPERATURE=1.0 water
// line of -81mm; the fraction itself had to move). Solved directly from
// two real measurements this time (dig limit -41.8mm, mound limit
// +80mm) rather than guessed: LIVING_RANGE_FRACTION = TEMPERATURE(0.745)
// - waterLevelFrac(for a -35mm water line on this box's calibrated
// range) = 0.368. Live-tunable in the Vegetation panel regardless.
float VegetationField::LIVING_RANGE_FRACTION = 0.368f;
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
// Hydrology second slice - REPLACES the periodic depression-fill pooling
// mechanic (ENABLE_POOLING/POOL_RECOMPUTE_INTERVAL_SEC/MAX_POOL_RISE_FRAC,
// all removed) with real per-frame flow - see the header note. Default on,
// same A/B posture ENABLE_SPREAD/the removed ENABLE_POOLING both used.
bool VegetationField::ENABLE_FLOW = true;
// Virtual-pipe model (2026-10-20) - first guesses, untested on hardware;
// see the header note for what each controls.
float VegetationField::FLOW_GRAVITY = 80.0f;
float VegetationField::FLOW_DAMPING_PER_SEC = 0.3f;
int VegetationField::FLOW_SUBSTEPS = 2;
float VegetationField::FLOW_TERRAIN_SMOOTHING_SEC = 0.3f;
int VegetationField::SOURCE_EDGE = VegetationField::EDGE_LEFT;
int VegetationField::SINK_EDGE = VegetationField::EDGE_RIGHT;
float VegetationField::SOURCE_SPAN = 0.4f;
float VegetationField::INFLOW_MM_PER_SEC = 8.0f;
// Water table (2026-10-22) - first guesses, see the header note.
bool VegetationField::ENABLE_WATER_TABLE = true;
float VegetationField::WATER_TABLE_MM = -15.0f;
float VegetationField::SEEPAGE_MM_PER_SEC = 4.0f;
bool VegetationField::USE_FLAT_SEA = false;
bool VegetationField::DRAW_SIMULATED_WATER = true;
// First-guess, untested on real hardware - see the header note.
float VegetationField::MIN_VISIBLE_WATER_DEPTH_FRAC = 0.004f;
// FOUND (2026-09-28), under the pooling mechanic this replaces - see the
// header note. ~2mm-equivalent on this box's ~285mm calibrated range:
// with the asymmetric band (on above MIN + this, off at MIN), a cell
// needs ~3mm of water to count, and the smoothed terrain under it keeps
// depth jitter well below that.
float VegetationField::WATER_HYSTERESIS_FRAC = 0.007f;
// First-guess, untested on real hardware - carried over in rough
// magnitude from the old MAX_POOL_RISE_FRAC default - see the header note.
float VegetationField::MAX_WATER_DEPTH_FRAC = 0.05f;
// New departure from ELF's literal getCellColor(), see the header note -
// 0.05 (~13/255, ~13 successful nut growth ticks) is a first-guess
// starting point: enough to filter out a single-tick "flash to black"
// while still committing well before nut's glacial multi-thousand-tick
// path to full density. Untested on real hardware for whether it looks
// right - live-tunable in the Vegetation panel.
float VegetationField::NUT_VISIBILITY_THRESHOLD = 0.05f;
float VegetationField::FOOD_PER_FULL_CELL = 255.0f;
bool VegetationField::DEBUG_SHOW_SNOW = false;
// Default true on this branch (ss/terrain-reference-visuals) - see
// CLAUDE.md's "Terrain-reference-visuals branch" entry. This branch's
// whole purpose is a bare-terrain/water look matching the Barthele et
// al. Figure 5 reference, with no ELF simulation visuals (vegetation
// speckle or agent icons) at all, not just toggleable off. Still a real
// checkbox in the GUI, not hardcoded - can be turned back on to inspect
// vegetation state if ever needed.
// Was true on this branch line (no vegetation drawn); the shader draws
// vegetation again since the succession model (2026-10-28), so it is off.
bool VegetationField::DEBUG_HIDE_VEGETATION = false;
// Succession model (2026-10-28) - see SuccessionModel.h.
int VegetationField::VEGETATION_MODEL = VegetationField::MODEL_SITE_FEEDBACK;
float VegetationField::SOIL_TINT_STRENGTH = 0.35f;
int VegetationField::SITE_DEBUG_VIEW = VegetationField::SITE_VIEW_OFF;

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

	// Succession-model rebuild, Phase 1 - see EcologyParams.h. Defaults
	// to the designer (data-free) preset at startup; the Vegetation
	// panel's new "Ecological data source" section can switch to the
	// published preset live, same pattern as every other live-tunable
	// value in this class.
	ecologyParams.load(EcologyParams::Preset::Designer);
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

// Hydrology - virtual-pipe shallow-water model (Mei, Decaudin & Hu 2007,
// "Fast Hydraulic Erosion Simulation and Visualization on GPU"), CPU-side,
// rewritten 2026-10-20. Replaces the earlier "move water proportional to
// the surface difference" scheme, which had no momentum: its water could
// only settle, so at rest nothing ever moved.
//
// Each cell has a water depth d on top of its (smoothed) terrain b, and
// four outflow "pipes" to its neighbours carrying a flux f >= 0 that
// persists between steps. Per substep:
//   1. Source: add INFLOW_MM_PER_SEC along SOURCE_EDGE.
//   2. Flux: each pipe accelerates by FLOW_GRAVITY * dt * (surface
//      difference) and decays by friction; negative flux clamps to 0.
//      The pipe across SINK_EDGE sees an outside surface at the cell's
//      own bed, so water drains off that edge; pipes across the other
//      edges are walls. If a cell's total outflow would exceed its depth
//      this substep, all four are scaled down (K factor) - a cell never
//      sends more water than it has, so depth cannot go negative and dry
//      cells (d = 0) send nothing.
//   3. Depth: d += dt * (inflow from neighbours' pipes - own outflow).
//      Velocity = net flux through the cell / average depth.
// Pipes have unit cross-section and length (one grid cell), folded into
// FLOW_GRAVITY. Units inside: mm and grid cells.
void VegetationField::stepFlow(float waterLevelFrac)
{
	uint64_t t0 = ofGetElapsedTimeMicros();

	float frameDt = ofGetLastFrameTime();
	// Guard the first frame / a debugger pause from feeding a huge step
	// into an explicit scheme.
	if (frameDt <= 0.0f || frameDt > 0.1f)
		frameDt = 1.0f / 60.0f;

	const int n = rows * cols;
	const float rangeMM = std::max(1.0f, elevationMax - elevationMin);

	// Smoothed terrain (mm) - see FLOW_TERRAIN_SMOOTHING_SEC.
	const float * elev = previousElevation.ptr<float>(0);
	float * terr = flowTerrain.ptr<float>(0);
	if (!flowTerrainReady) {
		for (int i = 0; i < n; i++)
			terr[i] = std::isfinite(elev[i]) ? elev[i] : 0.0f;
		flowTerrainReady = true;
	} else {
		float a = 1.0f - std::exp(-frameDt / std::max(0.01f, FLOW_TERRAIN_SMOOTHING_SEC));
		for (int i = 0; i < n; i++)
			if (std::isfinite(elev[i]))
				terr[i] += (elev[i] - terr[i]) * a;
	}

	// Work in mm for the duration of this call.
	float * depth = waterDepth.ptr<float>(0);
	for (int i = 0; i < n; i++)
		depth[i] *= rangeMM;

	float * fL = fluxL.ptr<float>(0);
	float * fR = fluxR.ptr<float>(0);
	float * fT = fluxT.ptr<float>(0);
	float * fB = fluxB.ptr<float>(0);
	float * vx = velX.ptr<float>(0);
	float * vy = velY.ptr<float>(0);

	const int substeps = std::max(1, std::min(16, FLOW_SUBSTEPS));
	const float dt = frameDt / substeps;
	const float damp = std::exp(-FLOW_DAMPING_PER_SEC * dt);
	const float g = FLOW_GRAVITY * dt;
	const bool sinkL = SINK_EDGE == EDGE_LEFT, sinkR = SINK_EDGE == EDGE_RIGHT;
	const bool sinkT = SINK_EDGE == EDGE_TOP, sinkB = SINK_EDGE == EDGE_BOTTOM;
	// Below this depth a cell's velocity is reported as 0 - dividing a
	// small flux by a near-zero depth would give meaningless huge speeds.
	const float VELOCITY_MIN_DEPTH_MM = 0.5f;

	float inflowVolume = 0.0f, outflowVolume = 0.0f, seepageVolume = 0.0f;

	for (int s = 0; s < substeps; s++) {
		inflowVolume += addSourceInflow(depth, dt);
		seepageVolume += addSeepage(depth, terr, dt);

		// Flux pass - reads depth, writes only this cell's own pipes.
		for (int gy = 0; gy < rows; gy++) {
			for (int gx = 0; gx < cols; gx++) {
				int idx = gy * cols + gx;
				float d = depth[idx];
				if (d <= 0.0f) {
					fL[idx] = fR[idx] = fT[idx] = fB[idx] = 0.0f;
					continue;
				}
				float h = terr[idx] + d;
				// Surface difference to each neighbour; across the sink
				// edge the outside surface is this cell's own bed, so the
				// difference is its depth; across a wall there is no pipe.
				float dhL = gx > 0 ? h - (terr[idx - 1] + depth[idx - 1]) : (sinkL ? d : 0.0f);
				float dhR = gx < cols - 1 ? h - (terr[idx + 1] + depth[idx + 1]) : (sinkR ? d : 0.0f);
				float dhT = gy > 0 ? h - (terr[idx - cols] + depth[idx - cols]) : (sinkT ? d : 0.0f);
				float dhB = gy < rows - 1 ? h - (terr[idx + cols] + depth[idx + cols]) : (sinkB ? d : 0.0f);
				float l = std::max(0.0f, fL[idx] * damp + g * dhL);
				float r = std::max(0.0f, fR[idx] * damp + g * dhR);
				float t = std::max(0.0f, fT[idx] * damp + g * dhT);
				float b = std::max(0.0f, fB[idx] * damp + g * dhB);
				float total = (l + r + t + b) * dt;
				if (total > d) {
					float k = d / total;
					l *= k; r *= k; t *= k; b *= k;
				}
				fL[idx] = l; fR[idx] = r; fT[idx] = t; fB[idx] = b;
			}
		}

		// Depth/velocity pass - reads fluxes, writes only this cell's
		// own depth, so updating depth in place is safe.
		for (int gy = 0; gy < rows; gy++) {
			for (int gx = 0; gx < cols; gx++) {
				int idx = gy * cols + gx;
				float inL = gx > 0 ? fR[idx - 1] : 0.0f;          // from left neighbour
				float inR = gx < cols - 1 ? fL[idx + 1] : 0.0f;   // from right neighbour
				float inT = gy > 0 ? fB[idx - cols] : 0.0f;       // from neighbour above
				float inB = gy < rows - 1 ? fT[idx + cols] : 0.0f; // from neighbour below
				float out = fL[idx] + fR[idx] + fT[idx] + fB[idx];
				float dOld = depth[idx];
				float dNew = std::max(0.0f, dOld + dt * (inL + inR + inT + inB - out));
				depth[idx] = dNew;

				// Flux leaving through the sink edge is water removed
				// from the table.
				if (sinkL && gx == 0) outflowVolume += fL[idx] * dt;
				if (sinkR && gx == cols - 1) outflowVolume += fR[idx] * dt;
				if (sinkT && gy == 0) outflowVolume += fT[idx] * dt;
				if (sinkB && gy == rows - 1) outflowVolume += fB[idx] * dt;

				float dAvg = 0.5f * (dOld + dNew);
				if (dAvg > VELOCITY_MIN_DEPTH_MM) {
					vx[idx] = 0.5f * (inL - fL[idx] + fR[idx] - inR) / dAvg;
					vy[idx] = 0.5f * (inT - fT[idx] + fB[idx] - inB) / dAvg;
				} else {
					vx[idx] = vy[idx] = 0.0f;
				}
			}
		}
	}

	// Optional legacy flat sea - see USE_FLAT_SEA.
	if (USE_FLAT_SEA) {
		for (int i = 0; i < n; i++) {
			float elevFrac = normalizedElevation(terr[i]);
			if (elevFrac < waterLevelFrac)
				depth[i] = std::max(depth[i], (waterLevelFrac - elevFrac) * rangeMM);
		}
	}

	// Diagnostics, then back to fraction-of-range storage.
	float volume = 0.0f, maxDepth = 0.0f;
	int wet = 0;
	float wetMM = MIN_VISIBLE_WATER_DEPTH_FRAC * rangeMM;
	for (int i = 0; i < n; i++) {
		float d = depth[i];
		volume += d;
		if (d > maxDepth) maxDepth = d;
		if (d > wetMM) wet++;
		depth[i] = d / rangeMM;
	}
	lastWaterVolume = volume;
	lastMaxDepthMM = maxDepth;
	lastWetCells = wet;
	lastInflowRate = inflowVolume / frameDt;
	lastSeepageRate = seepageVolume / frameDt;
	int below = 0;
	for (int i = 0; i < n; i++)
		if (terr[i] < WATER_TABLE_MM) below++;
	lastCellsBelowTable = below;
	lastOutflowRate = outflowVolume / frameDt;
	lastFlowMs = (ofGetElapsedTimeMicros() - t0) / 1000.0f;
}

float VegetationField::addSourceInflow(float * depthMM, float dt)
{
	if (INFLOW_MM_PER_SEC <= 0.0f || SOURCE_SPAN <= 0.0f)
		return 0.0f;
	bool vertical = SOURCE_EDGE == EDGE_LEFT || SOURCE_EDGE == EDGE_RIGHT;
	int length = vertical ? rows : cols;
	int span = std::max(1, (int)(length * std::min(1.0f, SOURCE_SPAN)));
	int first = (length - span) / 2;
	float add = INFLOW_MM_PER_SEC * dt;
	for (int i = first; i < first + span; i++) {
		int gx, gy;
		switch (SOURCE_EDGE) {
		case EDGE_LEFT:   gx = 0;        gy = i;        break;
		case EDGE_RIGHT:  gx = cols - 1; gy = i;        break;
		case EDGE_TOP:    gx = i;        gy = 0;        break;
		default:          gx = i;        gy = rows - 1; break;
		}
		depthMM[gy * cols + gx] += add;
	}
	return add * span;
}

float VegetationField::addSeepage(float * depthMM, const float * terrMM, float dt)
{
	if (!ENABLE_WATER_TABLE || SEEPAGE_MM_PER_SEC <= 0.0f)
		return 0.0f;
	const int n = rows * cols;
	const float rate = SEEPAGE_MM_PER_SEC * dt;
	float added = 0.0f;
	for (int i = 0; i < n; i++) {
		float room = WATER_TABLE_MM - (terrMM[i] + depthMM[i]);
		if (room > 0.0f) {
			float a = std::min(rate, room);
			depthMM[i] += a;
			added += a;
		}
	}
	return added;
}

// See the header note - read-only queries of stepFlow()'s state.
float VegetationField::getWaterDepthAt(int gx, int gy) const
{
	if (gx < 0 || gx >= cols || gy < 0 || gy >= rows)
		return 0.0f;
	return waterDepth.at<float>(gy, gx);
}

float VegetationField::getDepthBelowWaterTableAt(int gx, int gy) const
{
	if (!gridReady || !flowTerrainReady || gx < 0 || gx >= cols || gy < 0 || gy >= rows)
		return 0.0f;
	return flowTerrain.at<float>(gy, gx) - WATER_TABLE_MM;
}

ofVec2f VegetationField::getFlowVelocityAt(int gx, int gy) const
{
	if (gx < 0 || gx >= cols || gy < 0 || gy >= rows)
		return ofVec2f(0.0f, 0.0f);
	return ofVec2f(velX.at<float>(gy, gx), velY.at<float>(gy, gx));
}

bool VegetationField::isWaterCell(int gx, int gy) const
{
	if (!gridReady || gx < 0 || gx >= cols || gy < 0 || gy >= rows)
		return false;
	return waterState.at<float>(gy, gx) > 0.5f;
}

void VegetationField::resetWater()
{
	if (!gridReady)
		return;
	waterDepth.setTo(0.0f);
	fluxL.setTo(0.0f);
	fluxR.setTo(0.0f);
	fluxT.setTo(0.0f);
	fluxB.setTo(0.0f);
	velX.setTo(0.0f);
	velY.setTo(0.0f);
	waterState.setTo(0.0f);
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
	// A rebuilt grid starts dry, with no flux; water arrives from the
	// source (see stepFlow()).
	waterDepth = cv::Mat::zeros(rows, cols, CV_32F);
	fluxL = cv::Mat::zeros(rows, cols, CV_32F);
	fluxR = cv::Mat::zeros(rows, cols, CV_32F);
	fluxT = cv::Mat::zeros(rows, cols, CV_32F);
	fluxB = cv::Mat::zeros(rows, cols, CV_32F);
	velX = cv::Mat::zeros(rows, cols, CV_32F);
	velY = cv::Mat::zeros(rows, cols, CV_32F);
	flowTerrain = cv::Mat::zeros(rows, cols, CV_32F);
	flowTerrainReady = false;
	// Starts at "not water" everywhere - a sane default matching how
	// nothing is classified before update() has run at all; the very
	// first frame's hysteresis check has no meaningful prior state to
	// protect anyway, so this settles correctly within that first frame.
	waterState = cv::Mat::zeros(rows, cols, CV_32F);
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
			// Water classification now reads real per-cell depth (see
			// stepFlow(), called at the end of this function) instead of
			// an elevation/pool threshold - this is last frame's SETTLED
			// depth (stepFlow() runs after this loop using this frame's
			// freshly-sampled elevation, so next frame's loop sees an
			// up-to-date value) - same one-frame-lag pattern pooling's
			// recomputePools() used, just every frame instead of
			// periodically, since this per-cell cost is pure arithmetic.
			// With flow disabled, falls back to exactly the old flat
			// threshold (0 above sea, waterLevelFrac-elevFrac below it) -
			// same fallback shape isWaterAt() already uses outside the
			// grid.
			float depth = ENABLE_FLOW ? waterDepth.at<float>(gy, gx) : std::max(0.0f, waterLevelFrac - elevFrac);
			// Hysteresis - see WATER_HYSTERESIS_FRAC's header note
			// (found 2026-09-28 under the pooling mechanic this replaces:
			// raw instantaneous comparisons flicker at the shoreline from
			// ordinary sensor noise). A cell has to cross clearly past
			// MIN_VISIBLE_WATER_DEPTH_FRAC to change state; otherwise it
			// keeps whatever it already was.
			float & waterFlag = waterState.at<float>(gy, gx);
			bool wasWater = waterFlag > 0.5f;
			bool isWater = wasWater
				? (depth > MIN_VISIBLE_WATER_DEPTH_FRAC)
				: (depth > MIN_VISIBLE_WATER_DEPTH_FRAC + WATER_HYSTERESIS_FRAC);
			waterFlag = isWater ? 1.0f : 0.0f;
			bool isSnow = !isWater && elevFrac > snowLevelFrac;

			float & shrub = shrubDensity.at<float>(gy, gx);
			float & fruit = fruitDensity.at<float>(gy, gx);
			float & nut = nutDensity.at<float>(gy, gx);

			if (VEGETATION_MODEL == MODEL_SITE_FEEDBACK) {
				// Site-feedback model: vegetation is SuccessionModel's,
				// stepped after this loop; the ELF channels are left as
				// they are so switching back resumes the ELF state.
			} else if (isWater || isSnow) {
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
			// Alpha channel: snow reserved at 255 (flat, no gradation
			// needed), water's real depth packed into 1-249 (0 = dry/tie,
			// preserving a clean cutoff) scaled by MAX_WATER_DEPTH_FRAC -
			// replaces the old 0/128/255 tri-state flag now that the
			// shader reads real depth for its gradient instead of an
			// elevation-based proxy (see the .frag's header note).
			if (isSnow) {
				data[idx + 3] = 255;
			} else if (isWater) {
				float depthT = ofClamp(depth / MAX_WATER_DEPTH_FRAC, 0.0f, 1.0f);
				data[idx + 3] = (unsigned char)(1.0f + depthT * 248.0f);
			} else {
				data[idx + 3] = 0;
			}
		}
	}

	// Site-feedback model (Eq. 2.2) - see SuccessionModel.h. Uses this
	// frame's elevation, the water classification just updated above, and
	// last frame's smoothed bed relative to the water table.
	if (VEGETATION_MODEL == MODEL_SITE_FEEDBACK)
		stepSuccession(data);

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

	// Hydrology second slice - every frame, not throttled (unlike the
	// pooling this replaces) since the per-cell cost here is pure
	// arithmetic, no trig/random/sqrt - see the header note's cost
	// budget. Runs after the per-cell loop above so previousElevation is
	// fully refreshed for every cell this frame before stepFlow() reads
	// it - it's the same elevation sample already taken for the activity
	// computation, not a second Kinect query. This loop's own
	// classification just read waterDepth as it stood BEFORE this call -
	// same one-frame-lag pattern pooling's recomputePools() used.
	if (ENABLE_FLOW) {
		stepFlow(waterLevelFrac);
	}
}

void VegetationField::syncSuccessionParams()
{
	if (appliedParamsLoad == ecologyParams.getLoadCount())
		return;
	appliedParamsLoad = ecologyParams.getLoadCount();
	SuccessionModel::RoleParams roles[SuccessionModel::ROLE_COUNT];
	ecologyParams.toRoleParams(roles);
	std::string error;
	if (!succession.setParams(roles, error))
		ofLogWarning("VegetationField") << "succession parameters refused: " << error;
}

void VegetationField::stepSuccession(unsigned char * texData)
{
	syncSuccessionParams();
	const int n = rows * cols;
	succWater.resize(n);
	succAbove.resize(n);
	const float * water = waterState.ptr<float>(0);
	const float * terr = flowTerrainReady ? flowTerrain.ptr<float>(0) : previousElevation.ptr<float>(0);
	for (int i = 0; i < n; i++) {
		succWater[i] = water[i] > 0.5f ? 1 : 0;
		succAbove[i] = terr[i] - WATER_TABLE_MM;
	}
	float dt = ofGetLastFrameTime();
	if (dt <= 0.0f || dt > 0.1f)
		dt = 1.0f / 60.0f; // first frame / debugger pause, as stepFlow()

	SuccessionModel::Inputs in;
	in.elevationMM = previousElevation.ptr<float>(0); // this frame's samples (set in the loop above)
	in.water = succWater.data();
	in.heightAboveTableMM = succAbove.data();
	in.cols = cols;
	in.rows = rows;
	in.mmPerCell = mmPerCell;
	in.dt = dt;
	in.temperature = TEMPERATURE;
	in.restingTemperature = BASE_TEMPERATURE;
	in.spreadRadiusMM = SPREAD_RADIUS_MM;
	// The existing spontaneous chances (ELF's per-tick %), by role.
	in.spontaneousSeed[SuccessionModel::PIONEER] = FRUIT_GROWTH_CHANCE_PCT / 100.0f;
	in.spontaneousSeed[SuccessionModel::INTERMEDIATE] = NUT_GROWTH_CHANCE_PCT / 100.0f;
	in.spontaneousSeed[SuccessionModel::CLIMAX] = SHRUB_GROWTH_CHANCE_PCT / 100.0f;
	succession.step(in);
	if (succession.getCols() != cols || succession.getRows() != rows) {
		// No valid preset yet: draw no vegetation rather than the ELF state.
		for (int i = 0; i < n; i++)
			texData[i * 4 + 0] = texData[i * 4 + 1] = texData[i * 4 + 2] = 0;
		return;
	}

	// Texture: R/G/B = shrub/fruit/nut channels = climax/pioneer/
	// intermediate (EcologyParams' mapping); alpha (water) already set.
	const float * pio = succession.cover(SuccessionModel::PIONEER);
	const float * mid = succession.cover(SuccessionModel::INTERMEDIATE);
	const float * cli = succession.cover(SuccessionModel::CLIMAX);
	const float * soil = succession.soil();
	const float * moist = succession.moisture();
	if (!sitePixels.isAllocated() || (int)sitePixels.getWidth() != cols || (int)sitePixels.getHeight() != rows)
		sitePixels.allocate(cols, rows, OF_IMAGE_COLOR_ALPHA);
	unsigned char * site = sitePixels.getData();
	for (int i = 0; i < n; i++) {
		texData[i * 4 + 0] = (unsigned char)(cli[i] * 255.0f);
		texData[i * 4 + 1] = (unsigned char)(pio[i] * 255.0f);
		texData[i * 4 + 2] = (unsigned char)(mid[i] * 255.0f);
		site[i * 4 + 0] = (unsigned char)(soil[i] * 255.0f);
		site[i * 4 + 1] = (unsigned char)(moist[i] * 255.0f);
		site[i * 4 + 2] = 0;
		site[i * 4 + 3] = 255;
	}
	siteTex.loadData(sitePixels);
	siteTex.setTextureMinMagFilter(GL_NEAREST, GL_NEAREST);
}

int VegetationField::successionIndex(float kx, float ky) const
{
	int gx, gy;
	if (!cellIndexAt(kx, ky, gx, gy) || succession.getCols() != cols || succession.getRows() != rows)
		return -1;
	return gy * cols + gx;
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
	if (VEGETATION_MODEL == MODEL_SITE_FEEDBACK) {
		// Same preference order, on the model's cover: shrub (climax),
		// then fruit (pioneer).
		int i = successionIndex(kx, ky);
		if (i < 0) return 0.0f;
		float got = succession.eat(i, SuccessionModel::CLIMAX);
		if (got <= 0.0f) got = succession.eat(i, SuccessionModel::PIONEER);
		return got * FOOD_PER_FULL_CELL;
	}
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
	if (VEGETATION_MODEL == MODEL_SITE_FEEDBACK) {
		// Same preference order, on the model's cover: nut =
		// intermediate, fruit = pioneer.
		int i = successionIndex(kx, ky);
		if (i < 0) return 0.0f;
		int first = preferNut ? SuccessionModel::INTERMEDIATE : SuccessionModel::PIONEER;
		int second = preferNut ? SuccessionModel::PIONEER : SuccessionModel::INTERMEDIATE;
		float got = succession.eat(i, first);
		ateNut = preferNut;
		if (got <= 0.0f) { got = succession.eat(i, second); ateNut = !preferNut; }
		return got * FOOD_PER_FULL_CELL;
	}
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
	// Reads the SAME hysteresis-stabilized waterState update() already
	// maintains once per cell per frame - see WATER_HYSTERESIS_FRAC's
	// header note - rather than an independent raw check, so movement-
	// blocking/fishing (Critter/HumanAgent) always agrees with what's
	// actually rendered, and doesn't reintroduce the shoreline-flicker
	// bug on its own separate path. update() always runs before
	// critterController.update() each frame (see ofApp::update()), so
	// this is always this frame's fresh value, not stale.
	int gx, gy;
	if (cellIndexAt(kx, ky, gx, gy))
		return waterState.at<float>(gy, gx) > 0.5f;
	// Outside the grid entirely (shouldn't normally happen for a valid
	// agent position) - fall back to the flat instantaneous check.
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
	// Reference-only swatch, added 2026-10-16, REDRAWN same day once
	// terrainRamp() itself was rebuilt as a literal 6-stop match to
	// Barthele et al. Figure 5 (sampled directly from the reference
	// image, not paraphrased - see CLAUDE.md's "Literal Figure 5
	// colormap match" entry and heightMapShader.frag's header note).
	// This swatch answers "are these the right colors" completely
	// separately from "did I sculpt the right shape to reveal them," so
	// the two questions can't be conflated from a screenshot again.
	// Colors are hardcoded here and MUST stay in sync with
	// heightMapShader.frag's terrainRamp()/waterRamp() (both GL2/GL3
	// variants, per that file's own convention) - there is no shared
	// source of truth between GLSL and this ImGui draw call.
	ImGui::Text("Palette reference (NOT live sand data) - the exact terrainRamp()/");
	ImGui::Text("waterRamp() stops, so you can confirm this IS the Figure 5 look");
	ImGui::Text("regardless of what your current sand shape happens to be showing");
	ImGui::Text("right now. NOTE: this ramp is a literal match to Figure 5's own");
	ImGui::Text("rainbow/hypsometric colormap (teal->green->yellow->orange->maroon->");
	ImGui::Text("white) - a deliberate, explicit departure from the colorblind-safe");
	ImGui::Text("single-hue ramp this project used before, per direct user choice.");
	{
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		float barWidth = ImGui::GetContentRegionAvail().x;
		float barHeight = 24.0f;

		// terrainRamp(): teal -> green -> yellow/olive -> orange-brown ->
		// dark maroon -> pale near-white, sampled from Figure 5 directly.
		ImU32 stops[6] = {
			IM_COL32(0, 141, 167, 255),   // teal
			IM_COL32(64, 179, 89, 255),   // green
			IM_COL32(204, 199, 77, 255),  // yellow/olive
			IM_COL32(191, 115, 38, 255),  // orange-brown
			IM_COL32(115, 51, 40, 255),   // dark maroon
			IM_COL32(230, 230, 230, 255), // pale near-white (peak)
		};
		float segWidth = barWidth / 5.0f;
		ImVec2 p1 = ImGui::GetCursorScreenPos();
		for (int i = 0; i < 5; ++i) {
			ImVec2 segMin(p1.x + segWidth * i, p1.y);
			ImVec2 segMax(p1.x + segWidth * (i + 1), p1.y + barHeight);
			drawList->AddRectFilledMultiColor(segMin, segMax, stops[i], stops[i + 1], stops[i + 1], stops[i]);
		}
		ImGui::Dummy(ImVec2(barWidth, barHeight));
		ImGui::Text("terrainRamp() - bare land, low elevation (water's edge) -> high (snow line)");

		// waterRamp(): #00429d -> #4b73a2 (shown on its own; real water
		// also blends this with an underwater terrainRamp() "lakebed" at
		// 30-100%% opacity by depth - see the shader's water branch).
		ImU32 deep = IM_COL32(0, 66, 157, 255);
		ImU32 shallow = IM_COL32(75, 115, 162, 255);
		ImVec2 p2 = ImGui::GetCursorScreenPos();
		drawList->AddRectFilledMultiColor(p2, ImVec2(p2.x + barWidth, p2.y + barHeight), deep, shallow, shallow, deep);
		ImGui::Dummy(ImVec2(barWidth, barHeight));
		ImGui::Text("waterRamp() - deep -> shallow water (blends with the lakebed below it)");
	}
	ImGui::Text("If the swatch above looks right but the live sandbox still doesn't");
	ImGui::Text("match the Figure 5 reference, it's very likely vegetation speckle");
	ImGui::Text("covering the terrain ramp, not a color problem - Figure 5's images");
	ImGui::Text("are a bare DEM with no vegetation layer at all, but this project");
	ImGui::Text("always draws established vegetation on top, winner-take-all (no");
	ImGui::Text("fade), so land with any growth on it shows species speckle instead");
	ImGui::Text("of the ramp. Toggle this to see bare elevation only, for a real");
	ImGui::Text("apples-to-apples comparison against the reference image:");
	ImGui::Checkbox("Debug: hide vegetation (bare terrain ramp only)", &DEBUG_HIDE_VEGETATION);
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
	ImGui::Text("Hydrology: virtual-pipe water flow (Mei et al. 2007). Water enters");
	ImGui::Text("along the source edge, flows downhill with momentum, pools in");
	ImGui::Text("hollows, and drains off the sink edge.");
	ImGui::Checkbox("Enable flow (uncheck to A/B test performance)", &ENABLE_FLOW);
	{
		static const char * edges[] = { "Left", "Right", "Top", "Bottom" };
		ImGui::Combo("Source edge", &SOURCE_EDGE, edges, 4);
		ImGui::Combo("Sink edge", &SINK_EDGE, edges, 4);
	}
	ImGui::SliderFloat("Inflow (mm/s per source cell)", &INFLOW_MM_PER_SEC, 0.0f, 50.0f);
	ImGui::SliderFloat("Source span (fraction of edge)", &SOURCE_SPAN, 0.0f, 1.0f);
	ImGui::SliderFloat("Flow gravity", &FLOW_GRAVITY, 1.0f, 400.0f);
	ImGui::SliderFloat("Flow friction (1/s)", &FLOW_DAMPING_PER_SEC, 0.0f, 10.0f);
	ImGui::SliderInt("Flow substeps per frame", &FLOW_SUBSTEPS, 1, 8);
	ImGui::Text("If water shimmers or checkerboards, raise substeps or friction,");
	ImGui::Text("or lower gravity.");
	ImGui::SliderFloat("Terrain smoothing (s)", &FLOW_TERRAIN_SMOOTHING_SEC, 0.0f, 2.0f);
	ImGui::Checkbox("Water table (seepage into dug basins)", &ENABLE_WATER_TABLE);
	ImGui::SliderFloat("Water table (mm)", &WATER_TABLE_MM, -60.0f, 0.0f);
	ImGui::SliderFloat("Seepage (mm/s)", &SEEPAGE_MM_PER_SEC, 0.0f, 30.0f);
	ImGui::Text("Seepage in: %.0f/s (mm x cells^2), cells below the table: %d",
		lastSeepageRate, lastCellsBelowTable);
	ImGui::Checkbox("Also keep flat ELF sea (legacy)", &USE_FLAT_SEA);
	ImGui::Checkbox("Draw simulated water over the colour map", &DRAW_SIMULATED_WATER);
	if (ImGui::Button("Reset water (dry the table)"))
		resetWater();
	ImGui::Text("Flow cost: %.2f ms/frame, wet cells: %d", lastFlowMs, lastWetCells);
	ImGui::Text("Water volume: %.0f, in: %.0f/s, out: %.0f/s (mm x cells^2)",
		lastWaterVolume, lastInflowRate, lastOutflowRate);
	ImGui::Text("Max depth: %.1f mm", lastMaxDepthMM);
	if (gridReady) {
		int cx = cols / 2, cy = rows / 2;
		ofVec2f v = getFlowVelocityAt(cx, cy);
		ImGui::Text("ROI centre: depth %.1f mm, velocity (%.1f, %.1f) cells/s",
			getWaterDepthAt(cx, cy) * std::max(1.0f, elevationMax - elevationMin), v.x, v.y);
	}
	ImGui::Text("Steady state: 'in' and 'out' roughly equal, volume roughly flat.");
	ImGui::SliderFloat("Min visible water depth (fraction)", &MIN_VISIBLE_WATER_DEPTH_FRAC, 0.0f, 0.02f);
	ImGui::Text("Below this depth a cell doesn't count as 'visibly water' at all -");
	ImGui::Text("without it, a thin film mid-drain could flicker in and out at a");
	ImGui::Text("visually meaningless depth.");
	ImGui::Text("Water hysteresis (FOUND 2026-09-28, under the pooling mechanic this");
	ImGui::Text("replaces - 'flickering around the waterline'): a raw instantaneous");
	ImGui::Text("depth check flickers at any boundary from ordinary ~1-2mm sensor");
	ImGui::Text("noise alone. A cell must cross clearly past the threshold above by");
	ImGui::Text("this margin to change state at all - raise it if flicker is still");
	ImGui::Text("visible, lower it if the shoreline feels sluggish to respond to real");
	ImGui::Text("digging.");
	ImGui::SliderFloat("Water hysteresis (fraction)", &WATER_HYSTERESIS_FRAC, 0.0f, 0.05f);
	ImGui::Text("Max water depth (fraction) - scales the alpha-channel depth encoding");
	ImGui::Text("(and waterRamp()'s gradient): a cell at or above this real depth");
	ImGui::Text("renders at the darkest 'deep water' shade. First-guess, untested.");
	ImGui::SliderFloat("Max water depth (fraction)", &MAX_WATER_DEPTH_FRAC, 0.001f, 0.2f);
	ImGui::Separator();
	ImGui::Text("Nut visibility threshold (NOT in ELF): nut's color, (0,h,h),");
	ImGui::Text("has no channel pinned bright like shrub/fruit do, so a single");
	ImGui::Text("lucky growth tick at low elevation renders solid near-black -");
	ImGui::Text("confirmed on real hardware as random black speckle scattered");
	ImGui::Text("through otherwise-unrelated land, not a tie/negative-space cell");
	ImGui::Text("and not a contour line. Below this density, nut stays negative");
	ImGui::Text("space (like a real tie) instead of committing to that color.");
	ImGui::SliderFloat("Nut visibility threshold", &NUT_VISIBILITY_THRESHOLD, 0.0f, 0.3f);
	drawSuccessionGui();
	ecologyParams.drawGui();
	ImGui::End();
}

void VegetationField::drawSuccessionGui()
{
	ImGui::Separator();
	ImGui::Text("Succession");
	{
		static const char * models[] = { "ELF bands (Eq. 2.1)", "Site feedback (Eq. 2.2)" };
		ImGui::Combo("Vegetation model", &VEGETATION_MODEL, models, 2);
	}
	ImGui::SliderFloat("Soil tint strength", &SOIL_TINT_STRENGTH, 0.0f, 1.0f);
	{
		static const char * views[] = { "Off", "Moisture M (false colour)", "Soil S (false colour)" };
		ImGui::Combo("Debug overlay", &SITE_DEBUG_VIEW, views, 3);
	}
	if (VEGETATION_MODEL != MODEL_SITE_FEEDBACK)
		return;

	const SuccessionModel & m = succession;
	if (!m.hasParams())
		ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "No valid ecology preset loaded: the site model is not running.");
	ImGui::Text("Cover: pioneer %.2f, intermediate %.2f, climax %.2f; mean M %.2f, mean S %.2f",
		m.getMeanCover(SuccessionModel::PIONEER), m.getMeanCover(SuccessionModel::INTERMEDIATE),
		m.getMeanCover(SuccessionModel::CLIMAX), m.getMeanMoisture(), m.getMeanSoil());
	ImGui::Text("Disturbance events: %lld (last step %d); step cost %.2f ms",
		m.getDisturbanceEvents(), m.getDisturbedLastStep(), m.getLastStepMs());
	if (ImGui::Button("Reset vegetation and soil (bare ground, S = 0)"))
		succession.reset();

	if (ImGui::TreeNode("Succession rates")) {
		ImGui::Text("Vegetation (per second)");
		ImGui::SliderFloat("Growth rate, pioneer", &SuccessionModel::GROWTH_RATE[SuccessionModel::PIONEER], 0.0f, 3.0f);
		ImGui::SliderFloat("Growth rate, intermediate", &SuccessionModel::GROWTH_RATE[SuccessionModel::INTERMEDIATE], 0.0f, 3.0f);
		ImGui::SliderFloat("Growth rate, climax", &SuccessionModel::GROWTH_RATE[SuccessionModel::CLIMAX], 0.0f, 3.0f);
		ImGui::SliderFloat("Mortality rate", &SuccessionModel::MORTALITY_RATE, 0.0f, 3.0f);
		ImGui::SliderFloat("Suitability margin (x range width)", &SuccessionModel::SUITABILITY_MARGIN, 0.0f, 1.0f);
		ImGui::SliderFloat("Established cover (seed source)", &SuccessionModel::ESTABLISHED_COVER, 0.0f, 1.0f);
		ImGui::SliderFloat("Neighbour re-check chance", &SuccessionModel::SPREAD_CHECK_CHANCE, 0.0f, 1.0f);
		ImGui::Text("Spread radius and spontaneous chances: the ELF spread/growth sliders above.");
		ImGui::Text("Soil");
		ImGui::SliderFloat("Soil build rate", &SuccessionModel::SOIL_BUILD_RATE, 0.0f, 0.1f, "%.4f");
		ImGui::SliderFloat("Soil build weight, pioneer", &SuccessionModel::SOIL_BUILD_WEIGHT[SuccessionModel::PIONEER], 0.0f, 2.0f);
		ImGui::SliderFloat("Soil build weight, intermediate", &SuccessionModel::SOIL_BUILD_WEIGHT[SuccessionModel::INTERMEDIATE], 0.0f, 2.0f);
		ImGui::SliderFloat("Soil build weight, climax", &SuccessionModel::SOIL_BUILD_WEIGHT[SuccessionModel::CLIMAX], 0.0f, 2.0f);
		ImGui::SliderFloat("Soil loss rate (bare ground)", &SuccessionModel::SOIL_LOSS_RATE, 0.0f, 0.1f, "%.4f");
		ImGui::SliderFloat("Bare-ground cover threshold", &SuccessionModel::BARE_COVER, 0.0f, 0.5f);
		ImGui::SliderFloat("Max soil depth (cm, for cm/mm units)", &SuccessionModel::SOIL_MAX_DEPTH_CM, 1.0f, 200.0f);
		ImGui::Text("Moisture");
		ImGui::SliderFloat("Water-table falloff (mm)", &SuccessionModel::MOISTURE_TABLE_FALLOFF_MM, 1.0f, 100.0f);
		ImGui::SliderFloat("Soil retention", &SuccessionModel::SOIL_RETENTION, 0.0f, 1.0f);
		ImGui::SliderFloat("Wetting time (s)", &SuccessionModel::MOISTURE_WET_SEC, 0.1f, 30.0f);
		ImGui::SliderFloat("Drying time (s)", &SuccessionModel::MOISTURE_DRY_SEC, 1.0f, 600.0f);
		ImGui::SliderFloat("Temperature drying factor", &SuccessionModel::TEMPERATURE_DRYING, 0.0f, 50.0f);
		ImGui::Text("Disturbance");
		ImGui::SliderFloat("Disturbance threshold (mm)", &SuccessionModel::DISTURBANCE_MM, 2.0f, 40.0f);
		ImGui::SliderFloat("Soil retained after disturbance", &SuccessionModel::SOIL_RETAINED_FRACTION, 0.0f, 1.0f);
		ImGui::SliderFloat("Reference elevation time (s)", &SuccessionModel::DISTURBANCE_REFERENCE_SEC, 1.0f, 300.0f);
		ImGui::SliderFloat("Settle time after an event (s)", &SuccessionModel::DISTURBANCE_SETTLE_SEC, 0.0f, 10.0f);
		ImGui::Text("Terrain and cost");
		ImGui::SliderFloat("Elevation smoothing (s)", &SuccessionModel::ELEVATION_SMOOTH_SEC, 0.05f, 5.0f);
		ImGui::SliderInt("Slope blur radius (cells)", &SuccessionModel::SLOPE_BLUR_CELLS, 0, 10);
		ImGui::SliderInt("Rows per step (1 in N)", &SuccessionModel::UPDATE_INTERLEAVE, 1, 8);
		ImGui::TreePop();
	}
}
