/***********************************************************************
SeedParticles - visual seed particles where SuccessionModel's cover is
increasing (display only: never writes to the model).

- Growth signal: a snapshot of each role's cover is kept; when a window
  of WINDOW_SEC has passed (closed only after a whole multiple of the
  model's UPDATE_INTERLEAVE steps, so every row has had the same number of
  updates and the signal cannot stripe), per-cell growth is
  max(0, cover_now - cover_snapshot) per role, and a new snapshot is taken.
  The close is spread over UPDATE_INTERLEAVE frames, one row class per
  frame, so each row's window still spans the same number of model steps.
  Over the next window, seeds are spawned in proportion to that growth
  (SEEDS_PER_UNIT_GROWTH per unit of cover gained), at random points in
  the growing cells, never on water.
- Motion: a global wind (WIND_DIR_DEG, WIND_MM_PER_SEC) plus a smooth,
  slowly changing curl-noise field (divergence-free), plus a push away
  from the growth front (down the cover gradient of the seed's role, into
  bare ground). All scaled per role by SPEED (pioneer light and fast,
  intermediate about half, climax nearly still); lifetime per role.
- Optional "mortality motes" (default off): where cover decreases, motes
  in a desaturated role colour that stay put, shrink and fade.
- Colour: the role colour, mixed from a dim grey (the colour's luminance x
  0.45) to the full colour by soil S at the spawn cell; the grain dither in
  heightMapShader.frag uses the same formula (roleColour()).

Plain C++ (no openFrameworks); SeedLayer draws it. Positions are in grid
cells (x = column + fraction).

Ecosystem extension, part of the Shifting Sands fork of Magic Sand.
***********************************************************************/

#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

class SeedParticles
{
public:
	enum { PIONEER = 0, INTERMEDIATE = 1, CLIMAX = 2 };
	enum { KIND_SEED = 0, KIND_MOTE = 1 };

	// cover[r]: SuccessionModel::cover(r); soil: SuccessionModel::soil();
	// water: nonzero = water (may be null); interleave: the model's
	// UPDATE_INTERLEAVE. All read only. Resets on a grid change.
	void update(const float * const cover[3], const float * soil, const unsigned char * water,
		int cols, int rows, float mmPerCell, float dt, int interleave);
	void reset();

	std::size_t count() const { return x.size(); }
	float posX(std::size_t i) const { return x[i]; }
	float posY(std::size_t i) const { return y[i]; }
	int roleOf(std::size_t i) const { return role[i]; }
	int kindOf(std::size_t i) const { return kind[i]; }
	float alpha(std::size_t i) const;      // fade in, then out with age
	float ageOf(std::size_t i) const { return age[i]; }
	float sizeFactor(std::size_t i) const; // 1 for seeds; motes shrink
	const float * rgb(std::size_t i) const { return &col[i * 3]; }
	double lastUpdateMs() const { return lastMs; }
	long long spawnedTotal() const { return spawned; }

	// The role colour mixed from grey (luminance x 0.45) to full by soil.
	static void roleColour(int role, float soil, float out[3]);

	// Tunables (Vegetation panel -> Seeds).
	static bool ENABLED;
	static float WINDOW_SEC;
	static float SEEDS_PER_UNIT_GROWTH;
	static int MAX_PARTICLES;
	static float GROWTH_EPS;          // per-window growth below this is ignored
	static float WIND_DIR_DEG;        // direction the wind blows towards, 0 = +x
	static float WIND_MM_PER_SEC;
	static float CURL_MM_PER_SEC;
	static float CURL_SCALE_MM;       // size of the curl eddies
	static float CURL_PERIOD_SEC;     // how fast the curl field changes
	static float OUTWARD_MM_PER_SEC;  // push away from the growth front
	static float SPEED[3];            // per-role speed factor
	static float LIFE_SEC[3];         // per-role lifetime
	static bool MORTALITY_MOTES;
	static float MOTES_PER_UNIT_LOSS;
	static float MOTE_LIFE_SEC;
	static const float ROLE_RGB[3][3];

private:
	void spawn(int cell, int role, int kind, const float * soil, const float * const cover[3]);
	void removeAt(std::size_t i);
	void curl(float xmm, float ymm, float & vx, float & vy) const;
	float rnd();

	int cols = 0, rows = 0;
	float mmPerCell = 1.0f;
	double time = 0.0;
	// growth window
	std::vector<float> snap;          // 3 x cells
	bool haveSnap = false;
	float windowElapsed = 0.0f;
	int windowSteps = 0;
	// spawn schedule for the current window (cells x role), cumulative weights
	struct Pool { std::vector<int> cell; std::vector<float> cdf; float total = 0.0f; double acc = 0.0; float rate = 0.0f; };
	Pool seedPool[3], motePool[3];   // active (spawning now)
	Pool nextSeed[3], nextMote[3];   // being built while a window closes
	int closing = -1;                // row class being closed, -1 = none
	float closeLen = 0.5f;           // length of the window being closed, s
	// particles (structure of arrays)
	std::vector<float> x, y, vx, vy, age, life, col;
	std::vector<uint8_t> role, kind;
	// curl field, evaluated once per frame every CURL_STEP cells and
	// interpolated bilinearly per particle (eddies are ~60 mm, cells 1.9 mm)
	static const int CURL_STEP = 8;
	int curlW = 0, curlH = 0;
	std::vector<float> curlX, curlY;
	void curlAt(float gx, float gy, float & cx, float & cy) const;
	uint32_t rng = 0x9E3779B9u;
	double lastMs = 0.0;
	long long spawned = 0;
};
