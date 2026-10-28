/***********************************************************************
SuccessionModel - vegetation with site feedback (thesis Eq. 2.2):

    V_{t+1} = f(h_t, T_t, S_t),   S_{t+1} = g(S_t, V_t)

where V is the cover of three successional roles, h the terrain, T the
temperature and S the site state (soil and moisture), which persists
between steps and is changed by the vegetation itself. The ELF model it
sits beside (VegetationField's elevation bands, Eq. 2.1) has no S.

Per cell, per step (see the .cpp for the exact order):
- Moisture M (0..1): target 1 on water; elsewhere
  exp(-heightAboveTable / MOISTURE_TABLE_FALLOFF_MM) + SOIL_RETENTION * S,
  clamped to 1. M rises toward it with MOISTURE_WET_SEC and falls with
  MOISTURE_DRY_SEC, which temperature above its resting value shortens.
- Suitability per role: product of three range tests (moisture, slope in
  degrees, soil as a fraction of SOIL_MAX_DEPTH_CM), each 1 inside
  [min, max] and falling linearly to 0 over SUITABILITY_MARGIN x the
  range width outside it. The ranges come from EcologyParams.
- Vegetation: dV_r = GROWTH_RATE_r * suit_r * seed_r * (1 - total cover)
  - MORTALITY_RATE * (1 - suit_r) * V_r, total cover <= 1. seed_r is 1
  when an established same-role neighbour lies within the spread radius,
  otherwise the small spontaneous chance. No succession order is coded:
  it emerges from the soil ranges as the vegetation builds soil.
- Soil S (0..1): built by cover (SOIL_BUILD_RATE, per-role weights,
  saturating at 1), lost slowly on bare ground only.
- Disturbance: where the smoothed elevation departs from a slowly
  updated reference by more than DISTURBANCE_MM, the cell's vegetation
  is cleared and S is multiplied by SOIL_RETAINED_FRACTION once; the
  reference then follows the sand for DISTURBANCE_SETTLE_SEC so one dig
  is one event.

Plain C++ (no openFrameworks), so tools/succession_harness compiles this
exact file headless. VegetationField owns one instance and feeds it.

Ecosystem extension, part of the Shifting Sands fork of Magic Sand.
***********************************************************************/

#pragma once
#include <cstdint>
#include <string>
#include <vector>

class SuccessionModel
{
public:
	// Role order matches EcologyParams: pioneer (fruit channel),
	// intermediate (nut), climax (shrub).
	enum { PIONEER = 0, INTERMEDIATE = 1, CLIMAX = 2, ROLE_COUNT = 3 };

	// One tolerance range, in the unit its source reports.
	struct Range { float min = 0.0f; float max = 0.0f; std::string unit; };
	struct RoleParams { std::string name; Range moisture, slope, soilDepth; };

	struct Inputs {
		const float * elevationMM = nullptr;        // cols x rows, row-major; non-finite samples ignored
		const unsigned char * water = nullptr;      // nonzero = isWaterCell
		const float * heightAboveTableMM = nullptr; // getDepthBelowWaterTableAt (positive above the table)
		int cols = 0, rows = 0;
		float mmPerCell = 1.0f;
		float dt = 0.0f;                             // seconds
		float temperature = 0.0f, restingTemperature = 0.0f;
		float spreadRadiusMM = 40.0f;                // VegetationField::SPREAD_RADIUS_MM
		float spontaneousSeed[ROLE_COUNT] = { 0.03f, 0.02f, 0.01f }; // ELF growth chances / 100
	};

	// Checks that every range has a recognised unit and is not the
	// unfilled min == max == 0 placeholder. On failure, error says which
	// role, parameter and value.
	static bool validateParams(const RoleParams roles[ROLE_COUNT], std::string & error);
	// Validates, then replaces the active ranges; on failure the previous
	// ranges stay active and error says why.
	bool setParams(const RoleParams roles[ROLE_COUNT], std::string & error);
	bool hasParams() const { return paramsSet; }

	void step(const Inputs & in);
	// Clears all fields; the next step starts from bare ground, S = 0.
	void reset() { cols = rows = 0; }

	int getCols() const { return cols; }
	int getRows() const { return rows; }
	const float * cover(int role) const { return V[role].data(); }
	const float * moisture() const { return M.data(); }
	const float * soil() const { return S.data(); }
	// For tests and agents: direct field access.
	float * coverMutable(int role) { return V[role].data(); }
	float * soilMutable() { return S.data(); }
	// Removes a role's cover from a cell and returns what was there
	// (agents eating). idx = gy * cols + gx.
	float eat(int idx, int role);

	// Readouts from the last step (means over the rows it updated).
	float getMeanCover(int role) const { return meanCover[role]; }
	float getMeanMoisture() const { return meanM; }
	float getMeanSoil() const { return meanS; }
	long long getDisturbanceEvents() const { return disturbanceEvents; }
	int getDisturbedLastStep() const { return disturbedLastStep; }
	float getLastStepMs() const { return lastStepMs; }

	// Tunables (defaults in the .cpp; all live in the Vegetation panel's
	// Succession section).
	static float ELEVATION_SMOOTH_SEC;     // EMA of elevation, for slope and disturbance
	static int SLOPE_BLUR_CELLS;           // box-blur radius before the slope
	static float MOISTURE_TABLE_FALLOFF_MM;
	static float SOIL_RETENTION;
	static float MOISTURE_WET_SEC;
	static float MOISTURE_DRY_SEC;
	static float TEMPERATURE_DRYING;       // dry time / (1 + this x temperature above rest)
	static float SUITABILITY_MARGIN;       // fraction of a range's width
	static float GROWTH_RATE[ROLE_COUNT];  // per second
	static float ESTABLISHED_COVER;        // neighbour cover that counts as a seed source
	static int SPREAD_SAMPLES;
	static float SPREAD_CHECK_CHANCE;      // fraction of cells re-checking neighbours per step
	static float MORTALITY_RATE;           // per second, at suitability 0
	static float SOIL_BUILD_RATE;          // per second
	static float SOIL_BUILD_WEIGHT[ROLE_COUNT];
	static float SOIL_LOSS_RATE;           // per second, bare ground only
	static float BARE_COVER;               // total cover below which ground is bare
	static float SOIL_MAX_DEPTH_CM;        // converts cm/mm soil ranges to 0..1
	static float DISTURBANCE_MM;
	static float DISTURBANCE_REFERENCE_SEC;
	static float DISTURBANCE_SETTLE_SEC;
	static float SOIL_RETAINED_FRACTION;
	static int UPDATE_INTERLEAVE;          // update every Nth row per step, with N x dt

private:
	struct Band { float lo, hi, margin; };
	enum { PARAM_MOISTURE = 0, PARAM_SLOPE = 1, PARAM_SOIL = 2 };
	static bool convert(const Range & r, int param, Band & out, std::string & why);
	void resize(int c, int r);
	void blurElevation();
	void rebuildOffsets(int radiusCells);
	inline bool hasEstablishedNeighbor(int gx, int gy, int role);
	static const int OFFSET_COUNT = 512;
	inline uint32_t nextRandom() {
		rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5;
		return rngState;
	}

	RoleParams params[ROLE_COUNT];
	bool paramsSet = false;

	int cols = 0, rows = 0;
	bool ready = false;
	std::vector<float> V[ROLE_COUNT];
	std::vector<float> M, S;
	std::vector<float> elev, reference, hold;  // mm, mm, s
	std::vector<float> blurred, blurTmp, prefix;
	std::vector<unsigned char> seedBits;        // bit r: an established role-r neighbour was found
	std::vector<int> offsets;                   // (dx, dy) pairs within the spread radius
	int offsetRadius = -1;
	uint32_t rngState = 2463534242u;
	int phase = 0;                              // which rows the next step updates

	float meanCover[ROLE_COUNT] = { 0, 0, 0 };
	float meanM = 0.0f, meanS = 0.0f, lastStepMs = 0.0f;
	long long disturbanceEvents = 0;
	int disturbedLastStep = 0;
};
