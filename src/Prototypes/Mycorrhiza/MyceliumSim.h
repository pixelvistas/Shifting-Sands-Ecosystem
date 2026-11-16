/***********************************************************************
MyceliumSim - Prototype 2, network style "Mycelium" (default from
2026-11-14). A port of tools/mycelium_lookdev/index.html (the browser
look-dev page, the reference implementation) to real Kinect data. Plain C++
(no openFrameworks); MycorrhizaField owns one and feeds it the touch
footprint and the smoothed sand; MycorrhizaRenderer draws it with the
mycorrhiza/GL2|GL3/myc_* shaders. Display only: the succession model is
read, never written.

What is simulated here (the page's GPU passes, moved to the CPU where they
only need the coarse grids):
- Lit footprint, on the vegetation grid (one cell per Kinect pixel, ~2 mm):
  R lit = max(R, smoothstep(0.25, 0.55, footprint)) where the touch
  detection reports a touch (no fade-in tied to depth); the footprint is
  MycorrhizaField's closed, blurred mark, blurred once more here, so a
  single isolated marked cell never lights (its blurred value stays below
  0.25). R fades only with TRACE_MEM (1800 s = never). G: seconds since the
  cell was last touched (reset where the footprint > 0.3). Stroke direction
  (dirX, dirY): page fsMaskTouch took it from the pointer; here it is the
  direction from older to newer touch, -grad(G), fitted over cells within
  DIR_RADIUS_CELLS that were touched in the last DIR_WINDOW_SEC, blended
  into the stored value and kept when no good value is found.
- Liquid motion: the page's virtual-pipe shallow water (Mei, Decaudin and
  Hu 2007; fsFlux, fsDepth, fsVelL) on a half-resolution grid (~4 mm cells)
  over the smoothed sand, with Thickness (flux damping) and Stickiness
  (yield: surface differences below it do not flow). Touches pour
  POUR mm/s x lit footprint. It only supplies the velocity that carries
  the texture; it never decides what is lit, so light cannot drain away.
- Hyphae (page growThreads): tips spawned only at the outer edge of the lit
  mask (lit here, unlit 3 coarse cells further out, on a coarse grid
  refreshed every 5 steps as the page's read-back); curvature is an
  Ornstein-Uhlenbeck process per mm grown (CURVE, CORR), never per frame;
  branching per mm (BRANCH) after 8 mm; fusing with an unrelated thread's
  4 px occupancy cell after 15 mm of travel, or stopping on entering lit
  ground after 25 mm; tip lifetime TIP_LIFE. Also stops on the sandbox's own
  water. Segments are stored every SEGMENT_MM (the page stores one per
  frame; coalescing keeps the persistent mesh within MAX_SEGMENTS) with arc
  length s, phase, birth time, and persist with TRACE_MEM.

Units: positions of threads and segments are in "fibre pixels", the
renderer's grid-space image at FIB_PX pixels per grid cell (~1 px per mm,
as the page's "px ~ mm"). Distances given in mm are converted with
mmPerCell.
***********************************************************************/

#pragma once
#include <cstdint>
#include <deque>
#include <vector>

class MyceliumSim
{
public:
	struct Inputs {
		const float * footprint = nullptr;  // per cell, 0..1 (MycorrhizaField's closed, blurred mark; 0 outside touches)
		const float * sand = nullptr;       // per cell, settled sand elevation, mm (never raw depth)
		const unsigned char * water = nullptr;
		const float * cover[3] = { nullptr, nullptr, nullptr };
		const float * soil = nullptr;
		int cols = 0, rows = 0;
		float mmPerCell = 2.0f;
		float dt = 1.0f / 40.0f;
		bool adopting = false;              // settling after a (re)start: nothing is touched
	};

	struct Segment {
		float x0, y0, x1, y1;   // fibre px
		float width;            // fibre px (renderer enforces a minimum in projector px); 0 = removed
		float s0, s1;           // arc length along the thread, fibre px
		float phase, birth, hue;
		uint64_t id;
	};

	void step(const Inputs & in);
	void clear();

	int getCols() const { return cols; }
	int getRows() const { return rows; }
	int getLiquidCols() const { return lw; }
	int getLiquidRows() const { return lh; }
	double getTime() const { return time; }
	// Per cell: lit, seconds since touched, stroke direction (unit or 0).
	const float * lit() const { return R.data(); }
	const float * age() const { return G.data(); }
	const float * dirX() const { return DX.data(); }
	const float * dirY() const { return DY.data(); }
	// Per cell: warming from the model's recovery (WARM_FROM_MODEL): eased toward max(cover, S), reset by touch.
	const float * modelWarm() const { return MW.data(); }
	// Per cell: smoothed sand (mm), for the swirl's slope term and the shading.
	const float * smoothSand() const { return sandS.data(); }
	// Per liquid cell: depth (mm) and velocity (liquid cells per second), as the page's velL.
	const float * liquidDepth() const { return lH.data(); }
	const float * liquidVelX() const { return lVX.data(); }
	const float * liquidVelY() const { return lVY.data(); }
	const std::deque<Segment> & segments() const { return segs; }
	const std::vector<uint64_t> & segmentsRemovedThisStep() const { return removed; }
	// The texture's base pattern should be rebuilt (page: every 0.4 s while touching, and after a clear).
	bool baseDirty() const { return dirtyBase; }
	void baseRebuilt() { dirtyBase = false; lastBase = time; }
	void requestBaseRebuild() { dirtyBase = true; }
	bool touchedThisStep() const { return touchedNow; }
	int activeThreads() const;
	int edgeCount() const { return (int)edges.size(); }
	long long fusions() const { return fuses; }
	long long patchStops() const { return stopsOnLight; }
	float fadeRate() const;             // 1/s, 0 = never (TRACE_MEM >= 1800)
	float fibPxPerMm() const;           // FIB_PX / mmPerCell
	// Last step by part, ms: mask, liquid, threads (incl. edges).
	float partMs(int i) const { return (i >= 0 && i < 3) ? partTime[i] : 0.0f; }

	// Page settings (same names as the page's S object; page units).
	static float POUR;            // Liquid per touch, mm/s
	static float PIPE_K;          // Flow speed
	static float VISC;            // Thickness
	static float YIELD;           // Stickiness, mm
	static float EVAP_MEM;        // s for liquid to evaporate to 10 % (1800 = never)
	static float TRACE_MEM;       // Trace memory, s (1800 = never fade)
	static float SPAWN;           // per s per 100 mm of edge
	static float SPEED;           // mm/s
	static float CURVE;           // deg/cm
	static float CORR;            // mm (curve smoothness)
	static float BRANCH;          // per 100 mm
	static float TIP_LIFE;        // s
	static float WIDTH;           // px
	static float SLOPE;           // follow the slopes, 0..1
	static int MAX_THREADS;
	static bool THREADS_ON;
	// Port settings (not in the page).
	static int FIB_PX;            // fibre pixels per grid cell
	static float DIR_WINDOW_SEC;  // cells touched this recently define the stroke direction
	static int DIR_RADIUS_CELLS;
	static bool WARM_FROM_MODEL;  // drive the warming from the model's recovery instead of time
	static float MODEL_EASE_SEC;
	static float SEGMENT_MM;
	static int MAX_SEGMENTS;
	static int COARSE_CELLS;      // cells per coarse cell for edges and lit-ground tests (~7.6 mm; page 6.4 px)

private:
	void resize(int c, int r);
	void updateMask(const Inputs & in, float dt);
	void updateLiquid(const Inputs & in, float dt);
	void updateCoarse();
	void growThreads(const Inputs & in, float dt);
	float sampleCoarse(const std::vector<float> & a, float px, float py) const;
	void emit(int t, float x1, float y1, float s1);
	inline uint32_t rnd() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
	inline float rnd01() { return (rnd() >> 8) * (1.0f / 16777216.0f); }
	float randn();

	int cols = 0, rows = 0, lw = 0, lh = 0, cw = 0, ch = 0;
	float mm = 2.0f;
	double time = 0.0, lastBase = -1e9;
	bool dirtyBase = true, touchedNow = false;
	unsigned stepNo = 0;
	uint32_t rng = 0x2545F491u;
	std::vector<float> R, G, DX, DY, MW, fp2, sandS, tmp;
	std::vector<unsigned char> touched;
	// liquid (half resolution)
	std::vector<float> lT, lH, lHn, lF, lFn, lVX, lVY, lSrc; // lF: 4 per cell (L, R, T, B outflows)
	// coarse grid for edges, lit-ground tests and slopes
	std::vector<float> wet, terr;
	std::vector<int> edges;
	// threads
	struct Thread {
		float x, y, a, k, age, travel, s, sinceBranch, phase, hue;
		float sx, sy, ss;      // start of the segment being grown
		int gen;
		uint32_t id;
		bool alive;
	};
	std::vector<Thread> threads;
	std::vector<uint32_t> parentOf; // index = id
	std::vector<uint32_t> occ;
	int ow = 0, oh = 0;
	uint32_t nextId = 1;
	std::deque<Segment> segs;
	std::vector<uint64_t> removed;
	uint64_t nextSeg = 0;
	long long fuses = 0, stopsOnLight = 0;
	float partTime[3] = { 0, 0, 0 };
};
