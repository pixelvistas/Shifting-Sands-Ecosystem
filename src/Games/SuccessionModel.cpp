#include "SuccessionModel.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>

// Defaults - designer preset timing on moist flat ground (see
// docs/DEVLOG.md, 2026-10-28, for how they were tuned and the harness
// results they produce).
float SuccessionModel::ELEVATION_SMOOTH_SEC = 0.5f;
int SuccessionModel::SLOPE_BLUR_CELLS = 3;
float SuccessionModel::MOISTURE_TABLE_FALLOFF_MM = 16.0f;
float SuccessionModel::SOIL_RETENTION = 0.2f;
float SuccessionModel::MOISTURE_WET_SEC = 2.0f;
float SuccessionModel::MOISTURE_DRY_SEC = 60.0f;
float SuccessionModel::TEMPERATURE_DRYING = 10.0f;
float SuccessionModel::SUITABILITY_MARGIN = 0.25f;
float SuccessionModel::GROWTH_RATE[ROLE_COUNT] = { 0.5f, 0.5f, 0.4f };
float SuccessionModel::ESTABLISHED_COVER = 0.1f;
int SuccessionModel::SPREAD_SAMPLES = 3;
float SuccessionModel::SPREAD_CHECK_CHANCE = 0.1f;
float SuccessionModel::MORTALITY_RATE = 0.3f;
float SuccessionModel::SOIL_BUILD_RATE = 0.01f;
float SuccessionModel::SOIL_BUILD_WEIGHT[ROLE_COUNT] = { 1.0f, 0.5f, 0.3f };
float SuccessionModel::SOIL_LOSS_RATE = 0.005f;
float SuccessionModel::BARE_COVER = 0.05f;
float SuccessionModel::SOIL_MAX_DEPTH_CM = 30.0f;
float SuccessionModel::DISTURBANCE_MM = 8.0f;
float SuccessionModel::DISTURBANCE_REFERENCE_SEC = 30.0f;
float SuccessionModel::DISTURBANCE_SETTLE_SEC = 1.0f;
float SuccessionModel::SOIL_RETAINED_FRACTION = 0.6f;
// Each step updates every UPDATE_INTERLEAVE-th row (rotating), with
// UPDATE_INTERLEAVE x dt, so a cell updates at ~13 Hz at 40 fps. The
// dynamics take seconds to minutes, so the result is the same; a
// full-grid step costs ~5 ms on the development container (DEVLOG,
// 2026-10-28), which is too much on top of the rest of the frame.
int SuccessionModel::UPDATE_INTERLEAVE = 3;

static const char * PARAM_NAMES[3] = { "moisture", "slope", "soil depth" };

static std::string lowerTrim(const std::string & s)
{
	size_t i = 0;
	while (i < s.size() && std::isspace((unsigned char)s[i]))
		i++;
	std::string out = s.substr(i);
	for (char & c : out)
		c = (char)std::tolower((unsigned char)c);
	return out;
}

static bool startsWith(const std::string & s, const char * prefix)
{
	return s.compare(0, std::char_traits<char>::length(prefix), prefix) == 0;
}

// Converts one range to the model's internal units: moisture and soil as
// 0..1 fractions, slope in degrees. Units are matched on the start of the
// unit string, so a trailing note ("fraction (placeholder ...)") is fine.
bool SuccessionModel::convert(const Range & r, int param, Band & out, std::string & why)
{
	if (!std::isfinite(r.min) || !std::isfinite(r.max)) {
		why = "value is not a number";
		return false;
	}
	if (r.min == 0.0f && r.max == 0.0f) {
		why = "min and max are both 0 (unfilled placeholder)";
		return false;
	}
	if (r.min > r.max) {
		why = "min is greater than max";
		return false;
	}
	std::string u = lowerTrim(r.unit);
	float scale = 0.0f;
	bool slopeAsPercent = false;
	if (param == PARAM_MOISTURE) {
		if (startsWith(u, "fraction")) scale = 1.0f;
		else if (startsWith(u, "percent")) scale = 0.01f;
	} else if (param == PARAM_SLOPE) {
		if (startsWith(u, "degree")) scale = 1.0f;
		else if (startsWith(u, "percent")) slopeAsPercent = true; // "percent slope" (rise/run x 100)
	} else {
		if (startsWith(u, "fraction")) scale = 1.0f;
		else if (startsWith(u, "percent")) scale = 0.01f;
		else if (startsWith(u, "cm")) scale = 1.0f / std::max(0.1f, SOIL_MAX_DEPTH_CM);
		else if (startsWith(u, "mm")) scale = 1.0f / (10.0f * std::max(0.1f, SOIL_MAX_DEPTH_CM));
	}
	if (scale == 0.0f && !slopeAsPercent) {
		why = "unit \"" + r.unit + "\" is not recognised";
		return false;
	}
	if (slopeAsPercent) {
		const float toDeg = 57.29578f;
		out.lo = std::atan(r.min / 100.0f) * toDeg;
		out.hi = std::atan(r.max / 100.0f) * toDeg;
	} else {
		out.lo = r.min * scale;
		out.hi = r.max * scale;
	}
	out.margin = SUITABILITY_MARGIN * (out.hi - out.lo);
	return true;
}

bool SuccessionModel::validateParams(const RoleParams roles[ROLE_COUNT], std::string & error)
{
	for (int r = 0; r < ROLE_COUNT; r++) {
		const Range * ranges[3] = { &roles[r].moisture, &roles[r].slope, &roles[r].soilDepth };
		for (int p = 0; p < 3; p++) {
			Band b;
			std::string why;
			if (!convert(*ranges[p], p, b, why)) {
				error = (roles[r].name.empty() ? std::string("role ") + std::to_string(r) : roles[r].name)
					+ " " + PARAM_NAMES[p] + ": " + why;
				return false;
			}
		}
	}
	error.clear();
	return true;
}

bool SuccessionModel::setParams(const RoleParams roles[ROLE_COUNT], std::string & error)
{
	if (!validateParams(roles, error))
		return false;
	for (int r = 0; r < ROLE_COUNT; r++)
		params[r] = roles[r];
	paramsSet = true;
	return true;
}

void SuccessionModel::resize(int c, int r)
{
	cols = c;
	rows = r;
	size_t n = (size_t)c * r;
	for (int k = 0; k < ROLE_COUNT; k++) {
		V[k].assign(n, 0.0f);
	}
	seedBits.assign(n, 0);
	M.assign(n, 0.0f);
	S.assign(n, 0.0f);
	elev.assign(n, 0.0f);
	reference.assign(n, 0.0f);
	hold.assign(n, 0.0f);
	blurred.assign(n, 0.0f);
	blurTmp.assign(n, 0.0f);
	prefix.assign((size_t)(std::max(c, r) + 1) * std::max(c, r), 0.0f);
	ready = false;
}

// Separable box blur of elev into blurred (radius SLOPE_BLUR_CELLS, edges
// use the cells that exist), via running sums - O(n) whatever the radius.
void SuccessionModel::blurElevation()
{
	const int R = std::max(0, SLOPE_BLUR_CELLS);
	if (R == 0) {
		blurred = elev;
		return;
	}
	std::vector<float> & line = prefix;
	for (int y = 0; y < rows; y++) {
		const float * src = &elev[(size_t)y * cols];
		float * dst = &blurTmp[(size_t)y * cols];
		line[0] = 0.0f;
		for (int x = 0; x < cols; x++)
			line[x + 1] = line[x] + src[x];
		for (int x = 0; x < cols; x++) {
			int a = std::max(0, x - R), b = std::min(cols - 1, x + R);
			dst[x] = (line[b + 1] - line[a]) / (float)(b - a + 1);
		}
	}
	// Vertical pass, a row at a time: running column sums.
	std::vector<float> colSum(cols, 0.0f);
	int top = 0, bottom = -1; // rows currently in colSum: [top, bottom]
	for (int y = 0; y < rows; y++) {
		int a = std::max(0, y - R), b = std::min(rows - 1, y + R);
		while (bottom < b) {
			bottom++;
			const float * row = &blurTmp[(size_t)bottom * cols];
			for (int x = 0; x < cols; x++) colSum[x] += row[x];
		}
		while (top < a) {
			const float * row = &blurTmp[(size_t)top * cols];
			for (int x = 0; x < cols; x++) colSum[x] -= row[x];
			top++;
		}
		float inv = 1.0f / (float)(b - a + 1);
		float * dst = &blurred[(size_t)y * cols];
		for (int x = 0; x < cols; x++)
			dst[x] = colSum[x] * inv;
	}
}

// A fixed table of random offsets uniformly inside the spread disk, so a
// neighbour check costs table lookups instead of trig and sqrt.
void SuccessionModel::rebuildOffsets(int radiusCells)
{
	offsetRadius = radiusCells;
	offsets.clear();
	for (int i = 0; i < OFFSET_COUNT; i++) {
		float u = (nextRandom() & 0xFFFFFF) / 16777216.0f;
		float v = (nextRandom() & 0xFFFFFF) / 16777216.0f;
		float r = std::sqrt(u) * radiusCells;
		float a = v * 6.2831853f;
		offsets.push_back((int)std::lround(std::cos(a) * r));
		offsets.push_back((int)std::lround(std::sin(a) * r));
	}
}

bool SuccessionModel::hasEstablishedNeighbor(int gx, int gy, int role)
{
	const float * v = V[role].data();
	for (int i = 0; i < SPREAD_SAMPLES; i++) {
		int k = (int)(nextRandom() & (OFFSET_COUNT - 1)) * 2; // OFFSET_COUNT is a power of two
		int x = gx + offsets[k], y = gy + offsets[k + 1];
		if ((unsigned)x >= (unsigned)cols || (unsigned)y >= (unsigned)rows)
			continue;
		if (v[y * cols + x] > ESTABLISHED_COVER)
			return true;
	}
	return false;
}

float SuccessionModel::eat(int idx, int role)
{
	if (idx < 0 || idx >= cols * rows || role < 0 || role >= ROLE_COUNT)
		return 0.0f;
	float got = V[role][idx];
	V[role][idx] = 0.0f;
	return got;
}

// 1 inside [lo, hi], falling linearly to 0 over the margin outside it
// (invMargin = 1 / margin, or 0 for a hard edge).
static inline float rangeSuit(float x, float lo, float hi, float invMargin)
{
	if (x < lo) return invMargin > 0.0f ? std::max(0.0f, 1.0f - (lo - x) * invMargin) : 0.0f;
	if (x > hi) return invMargin > 0.0f ? std::max(0.0f, 1.0f - (x - hi) * invMargin) : 0.0f;
	return 1.0f;
}

void SuccessionModel::step(const Inputs & in)
{
	auto t0 = std::chrono::steady_clock::now();
	if (!paramsSet || in.cols <= 2 || in.rows <= 2 || !in.elevationMM || in.dt <= 0.0f)
		return;
	if (in.cols != cols || in.rows != rows)
		resize(in.cols, in.rows);
	const int n = cols * rows;
	const float dt = in.dt;

	// Active ranges, converted (cheap: nine ranges per step, so the
	// live-tunable SUITABILITY_MARGIN and SOIL_MAX_DEPTH_CM apply at once).
	Band band[ROLE_COUNT][3];
	for (int r = 0; r < ROLE_COUNT; r++) {
		std::string why;
		convert(params[r].moisture, PARAM_MOISTURE, band[r][0], why);
		convert(params[r].slope, PARAM_SLOPE, band[r][1], why);
		convert(params[r].soilDepth, PARAM_SOIL, band[r][2], why);
	}

	// Elevation: exponential smoothing in time.
	const float aElev = ready ? 1.0f - std::exp(-dt / std::max(0.01f, ELEVATION_SMOOTH_SEC)) : 1.0f;
	for (int i = 0; i < n; i++) {
		float e = in.elevationMM[i];
		if (std::isfinite(e))
			elev[i] += (e - elev[i]) * aElev;
	}
	if (!ready) {
		reference = elev;
		std::fill(hold.begin(), hold.end(), 0.0f);
	}
	blurElevation();

	// Lookup tables: moisture falloff exp(-a/F), and slope in degrees from
	// the squared gradient, atan(sqrt(g2)) - no exp, atan or sqrt per cell.
	const int LUT = 4096;
	static float expLut[LUT + 1];
	static float slopeLut[LUT + 1];
	static bool slopeReady = false;
	const float falloff = std::max(0.1f, MOISTURE_TABLE_FALLOFF_MM);
	const float expMax = 8.0f * falloff, expScale = LUT / expMax;
	for (int k = 0; k <= LUT; k++)
		expLut[k] = std::exp(-(k / expScale) / falloff);
	const float g2Max = 64.0f, g2Scale = LUT / g2Max; // gradients up to 8 (83 degrees)
	if (!slopeReady) {
		for (int k = 0; k <= LUT; k++)
			slopeLut[k] = std::atan(std::sqrt(k / g2Scale)) * 57.29578f;
		slopeReady = true;
	}

	const int interleave = std::max(1, UPDATE_INTERLEAVE);
	if (phase >= interleave) phase = 0;
	const float dtCell = dt * interleave;
	const float aRef = 1.0f - std::exp(-dtCell / std::max(0.1f, DISTURBANCE_REFERENCE_SEC));
	const float aWet = 1.0f - std::exp(-dtCell / std::max(0.01f, MOISTURE_WET_SEC));
	const float warm = std::max(0.0f, in.temperature - in.restingTemperature);
	const float dryTau = std::max(0.01f, MOISTURE_DRY_SEC) / (1.0f + TEMPERATURE_DRYING * warm);
	const float aDry = 1.0f - std::exp(-dtCell / dryTau);
	const float invDx = 1.0f / std::max(0.01f, in.mmPerCell);

	int radiusCells = std::max(1, (int)(in.spreadRadiusMM / std::max(0.01f, in.mmPerCell)));
	if (radiusCells != offsetRadius)
		rebuildOffsets(radiusCells);
	const uint32_t checkThreshold = (uint32_t)(std::min(1.0f, std::max(0.0f, SPREAD_CHECK_CHANCE)) * 4294967295.0);

	// Per-role constants for the inner loop.
	float lo[ROLE_COUNT][3], hi[ROLE_COUNT][3], invMargin[ROLE_COUNT][3];
	for (int r = 0; r < ROLE_COUNT; r++)
		for (int p = 0; p < 3; p++) {
			lo[r][p] = band[r][p].lo;
			hi[r][p] = band[r][p].hi;
			invMargin[r][p] = band[r][p].margin > 0.0f ? 1.0f / band[r][p].margin : 0.0f;
		}
	float growth[ROLE_COUNT], seedSpont[ROLE_COUNT];
	for (int r = 0; r < ROLE_COUNT; r++) {
		growth[r] = GROWTH_RATE[r] * dtCell;
		seedSpont[r] = in.spontaneousSeed[r];
	}
	const float mortality = MORTALITY_RATE * dtCell;
	const float soilBuild = SOIL_BUILD_RATE * dtCell, soilLoss = SOIL_LOSS_RATE * dtCell;
	const float w0 = SOIL_BUILD_WEIGHT[0], w1 = SOIL_BUILD_WEIGHT[1], w2 = SOIL_BUILD_WEIGHT[2];
	const float distMM = DISTURBANCE_MM, retained = SOIL_RETAINED_FRACTION, settle = DISTURBANCE_SETTLE_SEC;
	const float soilRetention = SOIL_RETENTION, bare = BARE_COVER;

	float * V0 = V[0].data(); float * V1 = V[1].data(); float * V2 = V[2].data();
	float * Mp = M.data(); float * Sp = S.data();
	float * E = elev.data(); float * Ref = reference.data(); float * Hold = hold.data();
	const float * Bl = blurred.data();
	unsigned char * seedP = seedBits.data();
	const unsigned char * water = in.water;
	const float * aboveP = in.heightAboveTableMM;

	float sumV0 = 0, sumV1 = 0, sumV2 = 0;
	double sumM = 0.0, sumS = 0.0;
	disturbedLastStep = 0;

	int updated = 0;
	for (int gy = phase; gy < rows; gy += interleave) {
		updated += cols;
		const float * rowC = Bl + (size_t)gy * cols;
		const float * rowT = Bl + (size_t)(gy > 0 ? gy - 1 : gy) * cols;
		const float * rowB = Bl + (size_t)(gy < rows - 1 ? gy + 1 : gy) * cols;
		const float yScale = invDx / (float)((gy < rows - 1 ? gy + 1 : gy) - (gy > 0 ? gy - 1 : gy));
		for (int gx = 0; gx < cols; gx++) {
			const int i = gy * cols + gx;
			float v0 = V0[i], v1 = V1[i], v2 = V2[i];

			// Disturbance: one event per dig/fill, then the reference
			// follows the sand while it settles.
			bool cleared = false;
			const float e = E[i];
			if (Hold[i] > 0.0f) {
				Hold[i] -= dtCell;
				Ref[i] = e;
				cleared = true;
			} else if (std::fabs(e - Ref[i]) > distMM) {
				Sp[i] *= retained;
				Ref[i] = e;
				Hold[i] = settle;
				cleared = true;
				disturbedLastStep++;
			} else {
				Ref[i] += (e - Ref[i]) * aRef;
			}
			if (cleared)
				v0 = v1 = v2 = 0.0f;

			const float s = Sp[i];

			// Moisture.
			float target;
			if (water && water[i]) {
				target = 1.0f;
			} else {
				float above = aboveP ? aboveP[i] : expMax;
				float base;
				if (!(above > 0.0f)) base = 1.0f;           // at or below the table (or NaN)
				else if (above >= expMax) base = 0.0f;
				else base = expLut[(int)(above * expScale)];
				target = std::min(1.0f, base + soilRetention * s);
			}
			float m = Mp[i];
			m += (target - m) * (target > m ? aWet : aDry);
			Mp[i] = m;

			// Slope (degrees) from the blurred, smoothed elevation.
			int xl = gx > 0 ? gx - 1 : gx, xr = gx < cols - 1 ? gx + 1 : gx;
			float ddx = (rowC[xr] - rowC[xl]) * invDx / (float)(xr - xl);
			float ddy = (rowB[gx] - rowT[gx]) * yScale;
			float g2 = ddx * ddx + ddy * ddy;
			float slopeDeg = g2 < g2Max ? slopeLut[(int)(g2 * g2Scale)] : std::atan(std::sqrt(g2)) * 57.29578f;

			// Neighbour seed sources, re-checked on a random fraction of
			// cells per step (the flags persist in between).
			unsigned char bits = seedP[i];
			if (nextRandom() < checkThreshold) {
				bits = 0;
				for (int r = 0; r < ROLE_COUNT; r++)
					if (hasEstablishedNeighbor(gx, gy, r)) bits |= (unsigned char)(1 << r);
				seedP[i] = bits;
			}

			// Vegetation: growth into free space, mortality where unsuitable.
			const float vOld[ROLE_COUNT] = { v0, v1, v2 };
			float vNew[ROLE_COUNT];
			const float space = std::max(0.0f, 1.0f - (v0 + v1 + v2));
			for (int r = 0; r < ROLE_COUNT; r++) {
				float suit = rangeSuit(m, lo[r][0], hi[r][0], invMargin[r][0]);
				if (suit > 0.0f) suit *= rangeSuit(slopeDeg, lo[r][1], hi[r][1], invMargin[r][1]);
				if (suit > 0.0f) suit *= rangeSuit(s, lo[r][2], hi[r][2], invMargin[r][2]);
				float seed = (bits >> r) & 1 ? 1.0f : seedSpont[r];
				float v = vOld[r] + growth[r] * suit * seed * space - mortality * (1.0f - suit) * vOld[r];
				vNew[r] = std::min(1.0f, std::max(0.0f, v));
			}
			if (cleared)
				vNew[0] = vNew[1] = vNew[2] = 0.0f; // stays bare while the sand settles
			float total = vNew[0] + vNew[1] + vNew[2];
			if (total > 1.0f) {
				float k = 1.0f / total;
				vNew[0] *= k; vNew[1] *= k; vNew[2] *= k;
			}
			V0[i] = vNew[0]; V1[i] = vNew[1]; V2[i] = vNew[2];

			// Soil, from this step's starting cover: S_{t+1} = g(S_t, V_t).
			float sNew = s + soilBuild * (vOld[0] * w0 + vOld[1] * w1 + vOld[2] * w2) * (1.0f - s);
			if (vOld[0] + vOld[1] + vOld[2] < bare)
				sNew -= soilLoss * s;
			sNew = std::min(1.0f, std::max(0.0f, sNew));
			Sp[i] = sNew;

			sumV0 += vNew[0]; sumV1 += vNew[1]; sumV2 += vNew[2];
			sumM += m;
			sumS += sNew;
		}
	}
	float sumV[ROLE_COUNT] = { sumV0, sumV1, sumV2 };
	ready = true;
	phase = (phase + 1) % interleave;
	disturbanceEvents += disturbedLastStep;
	// Means over the rows updated this step (a 1/UPDATE_INTERLEAVE sample).
	const float invN = 1.0f / (float)std::max(1, updated);
	for (int r = 0; r < ROLE_COUNT; r++)
		meanCover[r] = sumV[r] * invN;
	meanM = (float)(sumM * invN);
	meanS = (float)(sumS * invN);
	lastStepMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
}
