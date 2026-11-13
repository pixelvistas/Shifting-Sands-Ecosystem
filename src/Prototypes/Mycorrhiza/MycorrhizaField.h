/***********************************************************************
MycorrhizaField - Prototype 2 ("Mycorrhiza").

Two network styles (STYLE):
- STYLE_PATCHES_HYPHAE (default, 2026-11-13; MycorrhizaFieldLayers.cpp):
  the touched ground itself lights up (patch presence and maturity per
  cell) and thin hyphae grow from patch edges by tip growth, branch, fuse
  with other threads and reach other patches. Described in the .cpp.
- STYLE_PHYSARUM ("Physarum (previous)"), below: a Physarum-style agent
network (Jones 2010, "Characteristics of pattern formation and evolution
in approximations of Physarum transport networks") that grows out of the
places where participants touch the sand and stays behind as a slowly
fading trace.

It is a display module, not a simulation of fungi. It only READS the
succession model (cover, soil S, moisture M), the water mask and the
elevation grid; it writes nothing back, so the succession model's
outputs are identical with the module running or not
(tools/mycorrhiza_harness checks this bit for bit).

Per step (see the .cpp for the exact order):
- Touch: the elevation grid is averaged over TOUCH_BLOCK x TOUCH_BLOCK
  cell blocks; each block keeps a fast and a slow running mean
  (TOUCH_FAST_SEC, TOUCH_SLOW_SEC). A block is touched where they differ
  by more than TOUCH_MM, or where it has newly lost valid depth samples
  (a hand the Kinect pipeline rejects). The touch map rises with the
  excess and decays with TOUCH_MEMORY_SEC. For SETTLE_SEC after a
  (re)start every change is adopted as the untouched surface.
- Spawn: agents are born in touched blocks, SPAWN_PER_TOUCH per second at
  touch 1, with random headings, up to MAX_AGENTS. No touch, no agents.
- Sense and turn (Jones): three sensors at SENSOR_ANGLE_DEG and
  SENSOR_DIST_MM; turn by ROTATE_DEG toward the strongest. Sensed value:
  the trail map plus TRACE_SENSE x the trace map (so new growth joins
  the existing network); water and the grid edge read as repellent.
- Combing: heading bias toward the terrain's contour direction (the side
  closer to the current heading) and slow curl noise, COMB_WEIGHT.
- Move: STEP_MM x mix(MIN_MOBILITY, 1, max(S, M)) per step; a step into
  water or off the grid turns the agent around instead.
- Deposit: AGENT_DEPOSIT into the trail (attractant), TRACE_DEPOSIT into
  the trace (what is drawn), saturating at TRACE_SATURATION so busy
  strands do not become hot spots that out-shine the rest.
- Trail: 3x3 diffusion, decay TRAIL_DECAY per step, plus
  FOOD_DEPOSIT x total plant cover and TOUCH_DEPOSIT x touch.
- Trace: no diffusion (keeps the strands fibrous); decays to 10 % over
  TRACE_MEMORY_MIN minutes; cleared quickly under water.
- Ageing: agents die after AGENT_LIFE_SEC.

Two maps instead of the single trail map of the brief: a trail that is
diffused for minutes blurs into haze, so the persistent, drawn trace is
kept separate from the short-lived attractant that agents sense (DEVLOG,
2026-11-12).

Rates are per step at the nominal 40 steps per second and scaled by
dt x 40, so behaviour does not depend on the frame rate.

Plain C++ (no openFrameworks), compiled headless by
tools/mycorrhiza_harness. MycorrhizaRenderer owns one instance.

Prototype module of the Shifting Sands fork of Magic Sand; removable by
deleting src/Prototypes/Mycorrhiza and the "Display mode" entry in ofApp.
***********************************************************************/

#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <vector>

class MycorrhizaField
{
public:
	MycorrhizaField();
	~MycorrhizaField();
	MycorrhizaField(const MycorrhizaField &) = delete;
	MycorrhizaField & operator=(const MycorrhizaField &) = delete;

	struct Inputs {
		const float * elevationMM = nullptr;   // cols x rows, row-major; non-finite = no sample
		const float * cover[3] = { nullptr, nullptr, nullptr }; // succession roles, 0..1 (may be null)
		const float * soil = nullptr;          // S, 0..1 (may be null)
		const float * moisture = nullptr;      // M, 0..1 (may be null)
		const unsigned char * water = nullptr; // nonzero = water cell (may be null)
		int cols = 0, rows = 0;
		float mmPerCell = 2.0f;
		float dt = 1.0f / 40.0f;               // seconds
	};

	enum { STYLE_PATCHES_HYPHAE = 0, STYLE_PHYSARUM = 1 };
	static int STYLE;

	void step(const Inputs & in);
	// Empty network: no agents, zero trail/trace/touch; the touch
	// detector re-initialises from the next frame (that frame is never a touch).
	void clear();
	// Keeps the network but re-adopts the surface as untouched for SETTLE_SEC
	// (after a gap in the depth data, e.g. the Kinect image not stabilised).
	void restartTouch();

	int getCols() const { return cols; }
	int getRows() const { return rows; }
	const float * trace() const { return traceMap.data(); }
	const float * trail() const { return trailMap.data(); }
	// Per cell, RGBA8, for colour: R moisture M, G life = max(total cover, S),
	// B height (HEIGHT_LOW_MM .. HEIGHT_HIGH_MM -> 0..1), A 255.
	const unsigned char * site() const { return siteMap.data(); }
	const float * touchBlocks() const { return touch.data(); }
	int getBlockCols() const { return bw; }
	int getBlockSize() const { return blockSize; }
	int getBlockRows() const { return bh; }
	// Normalisation for display: trace / getDisplayLevel() in 0..1 (clamp).
	// A high percentile of the drawn trace, rising in ~2 s and falling
	// over LEVEL_FALL_SEC, never below LEVEL_FLOOR.
	float getDisplayLevel() const { return displayLevel; }

	// ---- Patches and hyphae (STYLE_PATCHES_HYPHAE) ----
	// Per cell, 0..1: presence (how lit the ground is) and maturity (colour:
	// 0 fresh cyan .. 1 amber; follows the model's recovery of the ground).
	const float * presence() const { return presenceMap.data(); }
	const float * maturity() const { return maturityMap.data(); }
	// Per touch block: blurred presence x maturity, for warming threads near mature patches.
	const float * warmthBlocks() const { return warmBlocks.data(); }
	struct Segment {
		float x0, y0, x1, y1;   // grid cells
		float width;            // projector px; 0 = removed (water)
		float birth;            // field time, s
		uint64_t id;            // running number (0, 1, 2, ... in order of creation)
	};
	struct Tip {
		float x, y, hx, hy;     // grid cells, unit heading
		float sx, sy;           // start of the segment being grown
		float bx, by;           // birth position (home patch)
		float age;              // s
		float grown;            // mm since leaving the home patch's tip line (inherited by branches)
		float run;              // mm grown by this tip itself
		float width;            // projector px
		int gen;
		uint32_t id, parent;
		bool outside;           // has left the patch it was born on
	};
	// Oldest first; ids are consecutive. Segments removed this step for
	// water are listed by id (width set to 0).
	const std::deque<Segment> & segments() const { return segs; }
	const std::vector<Tip> & tips() const { return tipList; }
	const std::vector<uint64_t> & segmentsRemovedThisStep() const { return removedIds; }
	double getTime() const { return time; }
	// Brightness of something born at field time b (fades to 10 % over TRACE_MEMORY_MIN).
	float fadeAt(float birth) const;
	int boundaryCellCount() const { return boundaryCount; }
	long long hyphaTipsSpawned() const { return tipsSpawned; }
	long long hyphaFusions() const { return fusions; }
	long long hyphaPatchConnections() const { return connections; }

	// Readouts.
	int agentCount() const { return (int)ax.size(); }
	long long spawnedTotal() const { return spawned; }
	int touchedBlocks() const { return touchedLastStep; }
	float maxTouchChangeMM() const { return maxChangeLastStep; }
	float getLastStepMs() const { return lastStepMs; }
	// Last step by phase. Physarum: touch, comb field, spawn+sort, agents, trail,
	// trace+maps, display level. Patches and hyphae: touch, patches, attractant, hyphae.
	enum { PHASE_COUNT = 7 };
	float getPhaseMs(int p) const { return (p >= 0 && p < PHASE_COUNT) ? phaseMs[p] : 0.0f; }

	// Tunables (defaults in the .cpp, live in the Mycorrhiza panel).
	static int MAX_AGENTS;
	static int TOUCH_BLOCK;            // cells per touch block side
	static float TOUCH_MM;             // change below this is never a touch
	static float TOUCH_FAST_SEC;
	static float TOUCH_SLOW_SEC;
	static float TOUCH_RISE_PER_SEC;   // touch gain at full excess (change >= 2 x TOUCH_MM)
	static float TOUCH_MEMORY_SEC;
	static float OCCLUSION_DROP;       // fall in valid-sample fraction that counts as a hand
	static float SETTLE_SEC;           // after a (re)start, changes are adopted, never touch
	static float SPAWN_PER_TOUCH;      // agents per second per block at touch 1
	static float SENSOR_ANGLE_DEG;
	static float SENSOR_DIST_MM;
	static float ROTATE_DEG;
	static float STEP_MM;              // per step at full mobility
	static float MIN_MOBILITY;
	static float COMB_WEIGHT;
	static float COMB_SLOPE_SAT;       // slope (mm/mm) at which contour combing is full strength
	static float CURL_SHARE;           // curl noise relative to contour combing
	static float CURL_SCALE_MM;
	static float CURL_PERIOD_SEC;
	static float AGENT_DEPOSIT;
	static float TRACE_DEPOSIT;
	static float TRACE_SENSE;
	static float TRACE_SATURATION;     // trace deposit slows to 0 as a cell approaches this
	static float FOOD_DEPOSIT;
	static float TOUCH_DEPOSIT;
	static float TRAIL_DECAY;          // fraction kept per step
	static float TRACE_MEMORY_MIN;     // minutes to fade to 10 %
	static float AGENT_LIFE_SEC;
	static float LEVEL_PERCENTILE;
	static float LEVEL_FALL_SEC;
	static float LEVEL_FLOOR;
	static float HEIGHT_LOW_MM;        // site height 0 (colour only)
	static float HEIGHT_HIGH_MM;       // site height 1 (colour only)
	static int THREADS;                // worker parts for the per-cell and agent loops (1 = single thread)
	// Patches.
	static float PATCH_CELL_MM;        // a cell in a touched block whose fast mean departs from its slow mean by this is marked
	static float PATCH_RISE_SEC;       // presence rises to 1 over this where marked
	static float MATURITY_EASE_SEC;    // maturity eases up toward the ground's life over this
	static float MATURITY_TIME_WEIGHT; // 0: colour is purely the model's recovery; > 0 blends in time since the mark
	static float MATURE_SEC;           // time since the mark at which the time term reaches 1
	// Hyphae.
	static float HYPHA_SPAWN_PER_SEC;  // tips per second per mm of patch boundary (x presence)
	static int MAX_TIPS;
	static int MAX_SEGMENTS;
	static float HYPHA_SPEED_MM_S;     // x mix(MIN_MOBILITY, 1, max(S, M))
	static float TURN_NOISE_DEG;       // heading random walk, degrees per sqrt(mm)
	static float ATTRACT_WEIGHT;       // pull toward other patches and plant cover, per mm grown
	static float ATTRACT_PLANT;        // plant cover in the attractant, relative to patch presence
	static float BRANCH_PER_MM;
	static int MAX_GENERATION;         // no branching beyond this generation
	static float HYPHA_WIDTH_PX;       // generation 0
	static float HYPHA_MIN_WIDTH_PX;   // deep generations
	static float TIP_LIFE_SEC;
	static float SEGMENT_MM;           // a segment is stored every this many mm
	static float HOME_MM;              // no pull and no patch stop within this much growth

private:
	struct Pool;
	std::unique_ptr<Pool> pool;
	void parallel(size_t n, const std::function<void(size_t, size_t, int)> & f);
	void resize(int c, int r);
	void updatePatches(const Inputs & in, float dt, bool adopting);
	void updateAttractant(const Inputs & in);
	void labelPatchBlocks();
	void spawnTips(const Inputs & in, float dt);
	void growTips(const Inputs & in, float dt);
	void pruneSegments(const Inputs & in);
	void clearLayers();
	float sampleBlocks(const std::vector<float> & f, float gx, float gy) const;
	void detectTouch(const Inputs & in, float dt);
	void updateCombField(const Inputs & in);
	void spawn(const Inputs & in, float dt);
	void sortAgents();
	void moveAgents(const Inputs & in, float k, float dt);
	void updateTrail(const Inputs & in, float k);
	void updateTrace(const Inputs & in, float dt);
	void updateDisplayLevel(float dt);
	inline uint32_t rnd() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
	inline float rnd01() { return (rnd() >> 8) * (1.0f / 16777216.0f); }

	int cols = 0, rows = 0, bw = 0, bh = 0, blockSize = 0;
	std::vector<float> trailMap, trailTmp, traceMap;
	// Touch blocks.
	std::vector<float> fastMean, slowMean, fastValid, slowValid, touch;
	bool touchInit = false;
	float settleLeft = 0.0f;
	// Comb field per block, interleaved: contour x, y (unit, x strength), curl x, y.
	std::vector<float> combField;
	// Per cell, built at the end of each step for the next one's agents.
	std::vector<float> senseMap, mobilityMap;
	std::vector<unsigned char> siteMap;
	// Agents (structure of arrays).
	std::vector<float> ax, ay, hx, hy, age;
	std::vector<float> sx, sy, shx, shy, sage; // sort scratch
	std::vector<int> bandStart;
	unsigned stepCount = 0;
	uint32_t rng = 0x9E3779B9u;
	double time = 0.0;
	float displayLevel = 0.0f;
	long long spawned = 0;
	int touchedLastStep = 0;
	float maxChangeLastStep = 0.0f;
	float lastStepMs = 0.0f;
	float phaseMs[PHASE_COUNT] = { 0, 0, 0, 0, 0, 0, 0 };
	std::vector<float> levelSample;

	// Patches.
	std::vector<float> cellFast, cellSlow, presenceMap, maturityMap, markTime, footprint;
	std::vector<unsigned char> mark, markTmp;
	// Per block: attractant (blurred presence + plant cover), warmth, patch labels, blur scratch.
	std::vector<float> attractBlocks, warmBlocks, blurTmp;
	std::vector<int> blockLabel, labelQueue;
	// Hyphae.
	std::deque<Segment> segs;
	std::vector<Tip> tipList;
	std::vector<uint64_t> removedIds;
	uint64_t nextSegmentId = 0;
	uint32_t nextTipId = 1;
	std::vector<uint32_t> occThread; // coarse occupancy (2 x 2 cells): thread of the last segment
	std::vector<float> occTime;
	int ow = 0, oh = 0;
	int boundaryCount = 0;
	long long tipsSpawned = 0, fusions = 0, connections = 0;
	inline float gauss() { // approx. N(0, 1) from 4 uniforms
		return (rnd01() + rnd01() + rnd01() + rnd01() - 2.0f) * 1.7320508f;
	}
};
