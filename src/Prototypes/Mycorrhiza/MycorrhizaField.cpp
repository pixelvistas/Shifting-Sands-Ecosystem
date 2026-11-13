#include "MycorrhizaField.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

// Defaults (DEVLOG, 2026-11-12: chosen with tools/mycorrhiza_harness, not
// yet checked on the sandbox). The brief's starting values (sensor angle 30,
// rotate 25, comb weight 0.15, one trail map) collapsed into a few thick
// ropes on the harness; the DEVLOG lists both sets.
int MycorrhizaField::MAX_AGENTS = 100000;
int MycorrhizaField::TOUCH_BLOCK = 4;
float MycorrhizaField::TOUCH_MM = 4.0f;
float MycorrhizaField::TOUCH_FAST_SEC = 0.15f;
float MycorrhizaField::TOUCH_SLOW_SEC = 1.5f;
float MycorrhizaField::TOUCH_RISE_PER_SEC = 4.0f;
float MycorrhizaField::TOUCH_MEMORY_SEC = 10.0f;
float MycorrhizaField::OCCLUSION_DROP = 0.3f;
float MycorrhizaField::SETTLE_SEC = 2.0f;
float MycorrhizaField::SPAWN_PER_TOUCH = 8.0f;
float MycorrhizaField::SENSOR_ANGLE_DEG = 45.0f;
float MycorrhizaField::SENSOR_DIST_MM = 18.0f;
float MycorrhizaField::ROTATE_DEG = 20.0f;
float MycorrhizaField::STEP_MM = 1.0f;
float MycorrhizaField::MIN_MOBILITY = 0.2f;
float MycorrhizaField::COMB_WEIGHT = 0.6f;
float MycorrhizaField::COMB_SLOPE_SAT = 0.15f;
float MycorrhizaField::CURL_SHARE = 0.8f;
float MycorrhizaField::CURL_SCALE_MM = 120.0f;
float MycorrhizaField::CURL_PERIOD_SEC = 40.0f;
float MycorrhizaField::AGENT_DEPOSIT = 2.0f;
float MycorrhizaField::TRACE_DEPOSIT = 0.2f;
float MycorrhizaField::TRACE_SENSE = 0.1f;
float MycorrhizaField::TRACE_SATURATION = 120.0f;
float MycorrhizaField::FOOD_DEPOSIT = 0.3f;
float MycorrhizaField::TOUCH_DEPOSIT = 0.02f;
float MycorrhizaField::TRAIL_DECAY = 0.8f;
float MycorrhizaField::TRACE_MEMORY_MIN = 5.0f;
float MycorrhizaField::AGENT_LIFE_SEC = 20.0f;
float MycorrhizaField::LEVEL_PERCENTILE = 0.98f;
float MycorrhizaField::LEVEL_FALL_SEC = 1200.0f;
float MycorrhizaField::LEVEL_FLOOR = 8.0f;

int MycorrhizaField::THREADS = 4;

static const float NOMINAL_STEPS_PER_SEC = 40.0f;
static const float REPEL = -1.0e6f;          // sensed value of water and the grid edge
static const float WATER_TRACE_FADE_SEC = 0.5f;
static const float LEVEL_RISE_SEC = 2.0f;
static const int LEVEL_STRIDE = 13;           // cells between display-level samples
static const float DEG = 3.14159265358979f / 180.0f;


// Persistent worker pool: run(parts, f) calls f(part) for part = 0..parts-1,
// part 0 on the calling thread, and returns when all are done.
struct MycorrhizaField::Pool {
	std::vector<std::thread> threads;
	std::mutex m;
	std::condition_variable wake, done;
	std::function<void(int)> job;
	int generation = 0, pending = 0;
	bool quit = false;
	explicit Pool(int workers) {
		for (int w = 0; w < workers; w++)
			threads.emplace_back([this, w] { loop(w + 1); });
	}
	~Pool() {
		{ std::lock_guard<std::mutex> l(m); quit = true; }
		wake.notify_all();
		for (std::thread & t : threads) t.join();
	}
	void loop(int part) {
		int seen = 0;
		for (;;) {
			std::function<void(int)> j;
			{
				std::unique_lock<std::mutex> l(m);
				wake.wait(l, [&] { return quit || generation != seen; });
				if (quit) return;
				seen = generation;
				j = job;
			}
			j(part);
			{ std::lock_guard<std::mutex> l(m); if (--pending == 0) done.notify_one(); }
		}
	}
	void run(const std::function<void(int)> & f) {
		{ std::lock_guard<std::mutex> l(m); job = f; pending = (int)threads.size(); generation++; }
		wake.notify_all();
		f(0);
		std::unique_lock<std::mutex> l(m);
		done.wait(l, [&] { return pending == 0; });
	}
};

MycorrhizaField::MycorrhizaField() {}
MycorrhizaField::~MycorrhizaField() {}

// Calls f(begin, end, part) over [0, n) split into contiguous parts.
void MycorrhizaField::parallel(size_t n, const std::function<void(size_t, size_t, int)> & f)
{
	const int parts = std::max(1, std::min(16, THREADS));
	if (parts == 1 || n < 1024) {
		f(0, n, 0);
		return;
	}
	if (!pool || (int)pool->threads.size() != parts - 1)
		pool.reset(new Pool(parts - 1));
	pool->run([&](int p) { f(n * p / parts, n * (p + 1) / parts, p); });
}

void MycorrhizaField::clear()
{
	ax.clear(); ay.clear(); hx.clear(); hy.clear(); age.clear();
	std::fill(trailMap.begin(), trailMap.end(), 0.0f);
	std::fill(traceMap.begin(), traceMap.end(), 0.0f);
	std::fill(touch.begin(), touch.end(), 0.0f);
	touchInit = false;
	settleLeft = SETTLE_SEC;
	displayLevel = 0.0f;
	spawned = 0;
}

void MycorrhizaField::restartTouch()
{
	touchInit = false;
	settleLeft = SETTLE_SEC;
}

void MycorrhizaField::resize(int c, int r)
{
	cols = c; rows = r;
	blockSize = std::max(1, TOUCH_BLOCK);
	bw = (cols + blockSize - 1) / blockSize;
	bh = (rows + blockSize - 1) / blockSize;
	const size_t n = (size_t)cols * rows, nb = (size_t)bw * bh;
	trailMap.assign(n, 0.0f); trailTmp.assign(n, 0.0f); traceMap.assign(n, 0.0f);
	fastMean.assign(nb, 0.0f); slowMean.assign(nb, 0.0f);
	fastValid.assign(nb, 0.0f); slowValid.assign(nb, 0.0f); touch.assign(nb, 0.0f);
	combField.assign(nb * 4, 0.0f);
	senseMap.assign(n, 0.0f); mobilityMap.assign(n, 0.0f);
	clear();
}

void MycorrhizaField::step(const Inputs & in)
{
	auto tStart = std::chrono::steady_clock::now();
	auto t0 = std::chrono::steady_clock::now();
	if (in.cols <= 0 || in.rows <= 0 || !in.elevationMM)
		return;
	if (in.cols != cols || in.rows != rows || std::max(1, TOUCH_BLOCK) != blockSize)
		resize(in.cols, in.rows);
	float dt = in.dt;
	if (!(dt > 0.0f) || dt > 0.1f)
		dt = 1.0f / NOMINAL_STEPS_PER_SEC;
	const float k = dt * NOMINAL_STEPS_PER_SEC;
	time += dt;

	auto lap = [&](int slot) { auto now = std::chrono::steady_clock::now(); phaseMs[slot] = std::chrono::duration<float, std::milli>(now - t0).count(); t0 = now; };
	detectTouch(in, dt); lap(0);
	updateCombField(in); lap(1);
	spawn(in, dt);
	if ((++stepCount & 7) == 0) sortAgents();
	lap(2);
	moveAgents(in, k, dt); lap(3);
	updateTrail(in, k); lap(4);
	updateTrace(in, dt); lap(5);
	updateDisplayLevel(dt); lap(6);

	lastStepMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - tStart).count();
}

// Block means of the elevation; a block is touched where its fast and
// slow means differ by more than TOUCH_MM, or where it has newly lost
// valid samples. Persistent gaps (sensor shadow) are equal in both means
// and never count.
void MycorrhizaField::detectTouch(const Inputs & in, float dt)
{
	const int B = blockSize;
	const float aF = 1.0f - std::exp(-dt / std::max(1e-3f, TOUCH_FAST_SEC));
	const float aS = 1.0f - std::exp(-dt / std::max(1e-3f, TOUCH_SLOW_SEC));
	const float decay = std::exp(-dt / std::max(1e-3f, TOUCH_MEMORY_SEC));
	const float thr = std::max(0.0f, TOUCH_MM);
	touchedLastStep = 0;
	maxChangeLastStep = 0.0f;
	for (int by = 0; by < bh; by++) {
		const int y0 = by * B, y1 = std::min(rows, y0 + B);
		for (int bx = 0; bx < bw; bx++) {
			const int x0 = bx * B, x1 = std::min(cols, x0 + B);
			float sum = 0.0f; int nValid = 0;
			for (int y = y0; y < y1; y++) {
				const float * row = in.elevationMM + (size_t)y * cols;
				for (int x = x0; x < x1; x++)
					if (std::isfinite(row[x])) { sum += row[x]; nValid++; }
			}
			const int nAll = (y1 - y0) * (x1 - x0);
			const float valid = nAll > 0 ? (float)nValid / nAll : 0.0f;
			const size_t b = (size_t)by * bw + bx;
			const float mean = nValid > 0 ? sum / nValid : 0.0f;
			if (!touchInit || settleLeft > 0.0f) { // (re)start: adopt the surface as it is
				fastMean[b] = slowMean[b] = mean;
				fastValid[b] = slowValid[b] = valid;
				continue;
			}
			if (nValid > 0) {
				if (slowValid[b] <= 0.0f) // first valid samples since start: adopt them, no touch
					fastMean[b] = slowMean[b] = mean;
				fastMean[b] += aF * (mean - fastMean[b]);
				slowMean[b] += aS * (mean - slowMean[b]);
			}
			fastValid[b] += aF * (valid - fastValid[b]);
			slowValid[b] += aS * (valid - slowValid[b]);
			float change = std::fabs(fastMean[b] - slowMean[b]);
			if (slowValid[b] - fastValid[b] > OCCLUSION_DROP)
				change = std::max(change, 2.0f * thr);
			maxChangeLastStep = std::max(maxChangeLastStep, change);
			float t = touch[b] * decay;
			if (change > thr && thr > 0.0f) {
				const float excess = std::min(1.0f, (change - thr) / thr);
				t += excess * TOUCH_RISE_PER_SEC * dt;
				touchedLastStep++;
			}
			touch[b] = std::min(1.0f, t);
		}
	}
	touchInit = true;
	settleLeft = std::max(0.0f, settleLeft - dt);
}

// Per block: the contour direction of the slow (settled) surface, scaled
// by slope up to COMB_SLOPE_SAT, and a slowly turning curl-noise vector.
void MycorrhizaField::updateCombField(const Inputs & in)
{
	const float cellMM = std::max(1e-3f, in.mmPerCell) * blockSize;
	const float L = std::max(1.0f, CURL_SCALE_MM);
	const float w = 6.2831853f * (float)(time / std::max(1.0f, CURL_PERIOD_SEC));
	const float p1 = w, p2 = 0.73f * w, p3 = 1.31f * w;
	for (int by = 0; by < bh; by++)
		for (int bx = 0; bx < bw; bx++) {
			const size_t b = (size_t)by * bw + bx;
			auto h = [&](int x, int y) {
				x = std::max(0, std::min(bw - 1, x)); y = std::max(0, std::min(bh - 1, y));
				const size_t i = (size_t)y * bw + x;
				return slowValid[i] > 0.0f ? slowMean[i] : slowMean[b];
			};
			const float gx = (h(bx + 1, by) - h(bx - 1, by)) / (2.0f * cellMM);
			const float gy = (h(bx, by + 1) - h(bx, by - 1)) / (2.0f * cellMM);
			const float g = std::sqrt(gx * gx + gy * gy);
			if (g > 1e-6f) {
				const float s = std::min(1.0f, g / std::max(1e-4f, COMB_SLOPE_SAT)) / g;
				combField[4 * b + 0] = -gy * s; combField[4 * b + 1] = gx * s;
			} else {
				combField[4 * b + 0] = combField[4 * b + 1] = 0.0f;
			}
			// Curl of psi = sin(a) cos(b) + 0.6 sin(c), scaled by L to O(1).
			const float px = (bx + 0.5f) * cellMM, py = (by + 0.5f) * cellMM;
			const float a = px / L + p1, bb = 0.83f * py / L + p2;
			const float c = 1.7f * (0.7f * px - 0.5f * py) / L + p3;
			const float dpdx = std::cos(a) * std::cos(bb) + 0.6f * 1.19f * std::cos(c);
			const float dpdy = -0.83f * std::sin(a) * std::sin(bb) - 0.6f * 0.85f * std::cos(c);
			combField[4 * b + 2] = CURL_SHARE * 0.5f * dpdy;
			combField[4 * b + 3] = -CURL_SHARE * 0.5f * dpdx;
		}
}

void MycorrhizaField::spawn(const Inputs & in, float dt)
{
	const int B = blockSize;
	const int cap = std::max(0, MAX_AGENTS);
	if ((int)ax.size() > cap) { // cap lowered in the GUI: drop the oldest-indexed extras
		ax.resize(cap); ay.resize(cap); hx.resize(cap); hy.resize(cap); age.resize(cap);
	}
	for (int by = 0; by < bh; by++)
		for (int bx = 0; bx < bw; bx++) {
			const float t = touch[(size_t)by * bw + bx];
			if (t <= 1e-4f)
				continue;
			const float lam = SPAWN_PER_TOUCH * t * dt;
			int n = (int)std::floor(lam + rnd01());
			for (; n > 0 && (int)ax.size() < cap; n--) {
				// Spread over the block and half a block around it (soft edge, no stair steps).
				const float x = std::min((float)cols - 1e-3f, std::max(0.0f, bx * B + (rnd01() * 2.0f - 0.5f) * B));
				const float y = std::min((float)rows - 1e-3f, std::max(0.0f, by * B + (rnd01() * 2.0f - 0.5f) * B));
				if (in.water && in.water[(size_t)(int)y * cols + (int)x])
					continue;
				const float a = rnd01() * 6.2831853f;
				ax.push_back(x); ay.push_back(y); hx.push_back(std::cos(a)); hy.push_back(std::sin(a));
				age.push_back(0.0f);
				spawned++;
			}
		}
}

// Counting sort of the agents by 4-row band, every 8th step, so agents
// next to each other in memory read neighbouring cells (the agent loop is
// bound by cache misses on the per-cell maps, not by arithmetic).
void MycorrhizaField::sortAgents()
{
	const size_t n = ax.size();
	if (n < 2)
		return;
	const int bands = (rows + 3) / 4 + 1;
	bandStart.assign(bands + 1, 0);
	for (size_t i = 0; i < n; i++) bandStart[(int)ay[i] / 4 + 1]++;
	for (int b = 0; b < bands; b++) bandStart[b + 1] += bandStart[b];
	sx.resize(n); sy.resize(n); shx.resize(n); shy.resize(n); sage.resize(n);
	for (size_t i = 0; i < n; i++) {
		const int d = bandStart[(int)ay[i] / 4]++;
		sx[d] = ax[i]; sy[d] = ay[i]; shx[d] = hx[i]; shy[d] = hy[i]; sage[d] = age[i];
	}
	ax.swap(sx); ay.swap(sy); hx.swap(shx); hy.swap(shy); age.swap(sage);
}

void MycorrhizaField::moveAgents(const Inputs & in, float k, float dt)
{
	const float mm = std::max(1e-3f, in.mmPerCell);
	const float sd = SENSOR_DIST_MM / mm;
	const float sa = SENSOR_ANGLE_DEG * DEG, ra = ROTATE_DEG * DEG;
	const float cs = std::cos(sa), ss = std::sin(sa), cr = std::cos(ra), sr = std::sin(ra);
	const float stepCells = std::max(0.0f, STEP_MM) / mm * k;
	const float comb = COMB_WEIGHT * k;
	const float traceDep = TRACE_DEPOSIT * k, trailDep = AGENT_DEPOSIT * k;
	const float invSat = 1.0f / std::max(1e-3f, TRACE_SATURATION);
	const int B = blockSize;
	float * trail = trailMap.data();
	float * tr = traceMap.data();
	// Built at the end of the previous step (updateTrace): what agents
	// sense (trail + TRACE_SENSE x trace; REPEL on water) and how far they
	// can move (mobility; negative on water).
	const float * sm = senseMap.data();
	const float * mob = mobilityMap.data();
	const float * comb4 = combField.data();
	const int W = cols, H = rows;
	const float fW = (float)W, fH = (float)H;

	auto sense = [&](float x, float y) -> float {
		if (x < 0.0f || y < 0.0f || x >= fW || y >= fH)
			return REPEL;
		return sm[(size_t)(int)y * W + (int)x];
	};

	const float life = AGENT_LIFE_SEC;
	const uint32_t seedBase = rnd();
	// Each part has its own random stream (deterministic for a given THREADS).
	// Deposits from neighbouring parts can collide on a cell at the part
	// boundary; agents are sorted by row band, so this is rare, and a lost
	// increment only changes how bright one cell is.
	parallel(ax.size(), [&](size_t begin, size_t end, int part) {
	uint32_t r = seedBase ^ (0x9E3779B9u * (uint32_t)(part + 1));
	if (!r) r = 1;
	auto prnd = [&]() { r ^= r << 13; r ^= r >> 17; r ^= r << 5; return r; };
	for (size_t i = begin; i < end; i++) {
		float x = ax[i], y = ay[i], dx = hx[i], dy = hy[i];
		float a = age[i] + dt;
		const size_t here = (size_t)(int)y * W + (int)x;
		const float mobHere = mob[here];
		if (a > life || mobHere < 0.0f) {
			age[i] = -1.0f; // dead (old, or flooded): removed below
			continue;
		}
		// Sense (Jones): front, left, right.
		const float lx = dx * cs - dy * ss, ly = dx * ss + dy * cs;
		const float rx = dx * cs + dy * ss, ry = -dx * ss + dy * cs;
		const float F = sense(x + dx * sd, y + dy * sd);
		const float FL = sense(x + lx * sd, y + ly * sd);
		const float FR = sense(x + rx * sd, y + ry * sd);
		int turn = 0; // +1 left (counter-clockwise in grid coordinates), -1 right
		if (F > FL && F > FR) turn = 0;
		else if (F < FL && F < FR) turn = (prnd() & 1) ? 1 : -1;
		else if (FL < FR) turn = -1;
		else if (FR < FL) turn = 1;
		if (turn != 0) {
			const float sgn = (float)turn;
			const float nx = dx * cr - dy * sr * sgn, ny = dx * sr * sgn + dy * cr;
			dx = nx; dy = ny;
		}
		// Combing toward the contour (whichever way is closer to the heading) plus curl.
		const float * cf = comb4 + 4 * ((size_t)((int)y / B) * bw + (int)x / B);
		float cx = cf[0], cy = cf[1];
		if (cx * dx + cy * dy < 0.0f) { cx = -cx; cy = -cy; }
		dx += comb * (cx + cf[2]);
		dy += comb * (cy + cf[3]);
		const float len = std::sqrt(dx * dx + dy * dy);
		if (len > 1e-6f) { dx /= len; dy /= len; } else { dx = 1.0f; dy = 0.0f; }
		// Ground gates movement: fastest through damp or living ground.
		const float s = stepCells * mobHere;
		const float nx = x + dx * s, ny = y + dy * s;
		bool blocked = nx < 0.0f || ny < 0.0f || nx >= fW || ny >= fH;
		size_t j = 0;
		if (!blocked) {
			j = (size_t)(int)ny * W + (int)nx;
			blocked = mob[j] < 0.0f; // water
		}
		if (blocked) {
			// Turn around, with a random spread of up to +-ROTATE.
			const float rr = ((prnd() >> 8) * (1.0f / 16777216.0f) * 2.0f - 1.0f) * ra;
			const float c = std::cos(rr), sn = std::sin(rr);
			const float tx = -dx, ty = -dy;
			dx = tx * c - ty * sn; dy = tx * sn + ty * c;
		} else {
			x = nx; y = ny;
			trail[j] += trailDep;
			tr[j] += traceDep * std::max(0.0f, 1.0f - tr[j] * invSat); // saturating: strands, not hot spots
		}
		ax[i] = x; ay[i] = y; hx[i] = dx; hy[i] = dy; age[i] = a;
	}
	});
	// Remove the dead, keeping order.
	size_t w = 0;
	for (size_t i = 0; i < ax.size(); i++) {
		if (age[i] < 0.0f) continue;
		if (w != i) { ax[w] = ax[i]; ay[w] = ay[i]; hx[w] = hx[i]; hy[w] = hy[i]; age[w] = age[i]; }
		w++;
	}
	ax.resize(w); ay.resize(w); hx.resize(w); hy.resize(w); age.resize(w);
}

// 3x3 mean diffusion and decay, plus food (plant cover) and touch
// attractants. Water holds no attractant.
void MycorrhizaField::updateTrail(const Inputs & in, float k)
{
	const int W = cols, H = rows, B = blockSize;
	const float decay = std::pow(std::min(1.0f, std::max(0.0f, TRAIL_DECAY)), k);
	const float food = FOOD_DEPOSIT * k, touchDep = TOUCH_DEPOSIT * k;
	const float * src = trailMap.data();
	float * dst = trailTmp.data();
	parallel((size_t)H, [&](size_t y0, size_t y1, int) {
	for (int y = (int)y0; y < (int)y1; y++) {
		const float * r0 = src + (size_t)std::max(0, y - 1) * W;
		const float * r1 = src + (size_t)y * W;
		const float * r2 = src + (size_t)std::min(H - 1, y + 1) * W;
		const float * tb = touch.data() + (size_t)(y / B) * bw;
		for (int x = 0; x < W; x++) {
			const int xl = std::max(0, x - 1), xr = std::min(W - 1, x + 1);
			float v = (r0[xl] + r0[x] + r0[xr] + r1[xl] + r1[x] + r1[xr] + r2[xl] + r2[x] + r2[xr]) * (1.0f / 9.0f);
			v *= decay;
			const size_t i = (size_t)y * W + x;
			if (in.water && in.water[i]) { dst[i] = 0.0f; continue; }
			float c = 0.0f;
			for (int r = 0; r < 3; r++) if (in.cover[r]) c += in.cover[r][i];
			v += food * std::min(1.0f, c) + touchDep * tb[x / B];
			dst[i] = v;
		}
	}
	});
	trailMap.swap(trailTmp);
}

// Trace decay (fast under water), then the maps the next step's agents
// read: sense = trail + TRACE_SENSE x trace (REPEL on water) and mobility
// = mix(MIN_MOBILITY, 1, max(S, M)) (-1 on water).
void MycorrhizaField::updateTrace(const Inputs & in, float dt)
{
	const float mem = std::max(0.05f, TRACE_MEMORY_MIN) * 60.0f;
	const float f = std::pow(0.1f, dt / mem);
	const float fw = f * std::exp(-dt / WATER_TRACE_FADE_SEC);
	const float ts = TRACE_SENSE;
	const float minMob = std::min(1.0f, std::max(0.0f, MIN_MOBILITY));
	const bool ground = in.soil || in.moisture;
	const size_t n = traceMap.size();
	float * t = traceMap.data();
	const float * trail = trailMap.data();
	float * sm = senseMap.data();
	float * mob = mobilityMap.data();
	parallel(n, [&](size_t i0, size_t i1, int) {
	for (size_t i = i0; i < i1; i++) {
		if (in.water && in.water[i]) {
			t[i] *= fw;
			sm[i] = REPEL;
			mob[i] = -1.0f;
			continue;
		}
		t[i] *= f;
		sm[i] = trail[i] + ts * t[i];
		float g = 1.0f;
		if (ground) {
			g = 0.0f;
			if (in.soil) g = in.soil[i];
			if (in.moisture) g = std::max(g, in.moisture[i]);
			g = std::min(1.0f, std::max(0.0f, g));
		}
		mob[i] = minMob + (1.0f - minMob) * g;
	}
	});
}

void MycorrhizaField::updateDisplayLevel(float dt)
{
	levelSample.clear();
	const size_t n = traceMap.size();
	for (size_t i = 0; i < n; i += LEVEL_STRIDE)
		if (traceMap[i] > 1e-3f) levelSample.push_back(traceMap[i]);
	float target = 0.0f;
	if (!levelSample.empty()) {
		const float p = std::min(1.0f, std::max(0.0f, LEVEL_PERCENTILE));
		size_t kth = std::min(levelSample.size() - 1, (size_t)(p * (levelSample.size() - 1)));
		std::nth_element(levelSample.begin(), levelSample.begin() + kth, levelSample.end());
		target = levelSample[kth];
	}
	const float tau = target > displayLevel ? LEVEL_RISE_SEC : std::max(1.0f, LEVEL_FALL_SEC);
	displayLevel += (target - displayLevel) * (1.0f - std::exp(-dt / tau));
	displayLevel = std::max(displayLevel, std::max(1e-3f, LEVEL_FLOOR));
}
