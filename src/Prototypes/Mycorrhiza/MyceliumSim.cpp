#include "MyceliumSim.h"
#include <algorithm>
#include <chrono>
#include <cmath>

// Page defaults (tools/mycelium_lookdev/index.html, DEFAULTS); no pasted
// settings were supplied with the port request (DEVLOG, 2026-11-14).
float MyceliumSim::POUR = 2.0f;
float MyceliumSim::PIPE_K = 30.0f;
float MyceliumSim::VISC = 6.0f;
float MyceliumSim::YIELD = 0.4f;
float MyceliumSim::EVAP_MEM = 900.0f;
float MyceliumSim::TRACE_MEM = 1800.0f;
float MyceliumSim::SPAWN = 0.3f;
float MyceliumSim::SPEED = 8.0f;
float MyceliumSim::CURVE = 40.0f;
float MyceliumSim::CORR = 14.0f;
float MyceliumSim::BRANCH = 6.0f;
float MyceliumSim::TIP_LIFE = 20.0f;
float MyceliumSim::WIDTH = 0.9f;
float MyceliumSim::SLOPE = 0.0f;
int MyceliumSim::MAX_THREADS = 500;
bool MyceliumSim::THREADS_ON = true;
// Port settings.
int MyceliumSim::FIB_PX = 2;
float MyceliumSim::DIR_WINDOW_SEC = 1.5f;
int MyceliumSim::DIR_RADIUS_CELLS = 3;
bool MyceliumSim::WARM_FROM_MODEL = false;
float MyceliumSim::MODEL_EASE_SEC = 3.0f;
float MyceliumSim::SEGMENT_MM = 3.0f;   // curvature varies over CORR (14 mm): 3 mm chords stay smooth
int MyceliumSim::MAX_SEGMENTS = 300000; // ~58 MB of GPU vertices (6 per segment)
int MyceliumSim::COARSE_CELLS = 4;

static const float PI_F = 3.14159265358979f;
static const float AGE_MAX = 9000.0f;
static const int LIQUID_SUBSTEPS = 3;          // page liquidStep: sub = 3
static const float NEVER = 1800.0f;            // page: a memory slider at its maximum means "never fade"

static float smoothstepf(float a, float b, float x) { float t = std::min(1.0f, std::max(0.0f, (x - a) / (b - a))); return t * t * (3 - 2 * t); }

float MyceliumSim::fadeRate() const { return TRACE_MEM >= NEVER ? 0.0f : std::log(10.0f) / std::max(1.0f, TRACE_MEM); }
float MyceliumSim::fibPxPerMm() const { return std::max(1, FIB_PX) / std::max(1e-3f, mm); }

float MyceliumSim::randn()
{
	float u = 0, v = 0;
	while (u == 0.0f) u = rnd01();
	while (v == 0.0f) v = rnd01();
	return std::sqrt(-2.0f * std::log(u)) * std::cos(2.0f * PI_F * v);
}

void MyceliumSim::clear()
{
	std::fill(R.begin(), R.end(), 0.0f);
	std::fill(G.begin(), G.end(), AGE_MAX);
	std::fill(DX.begin(), DX.end(), 0.0f);
	std::fill(DY.begin(), DY.end(), 0.0f);
	std::fill(MW.begin(), MW.end(), 0.0f);
	for (std::vector<float> * v : { &lH, &lHn, &lF, &lFn, &lVX, &lVY }) std::fill(v->begin(), v->end(), 0.0f);
	std::fill(occ.begin(), occ.end(), 0u);
	threads.clear();
	parentOf.assign(1, 0);
	nextId = 1;
	segs.clear();
	removed.clear();
	nextSeg = 0;
	edges.clear();
	fuses = stopsOnLight = 0;
	dirtyBase = true;
	lastBase = -1e9;
}

void MyceliumSim::resize(int c, int r)
{
	cols = c; rows = r;
	const size_t n = (size_t)c * r;
	for (std::vector<float> * v : { &R, &G, &DX, &DY, &MW, &fp2, &sandS, &tmp }) v->assign(n, 0.0f);
	touched.assign(n, 0);
	lw = (c + 1) / 2; lh = (r + 1) / 2;
	const size_t ln = (size_t)lw * lh;
	for (std::vector<float> * v : { &lT, &lH, &lHn, &lVX, &lVY, &lSrc }) v->assign(ln, 0.0f);
	lF.assign(ln * 4, 0.0f); lFn.assign(ln * 4, 0.0f);
	const int C = std::max(1, COARSE_CELLS);
	cw = (c + C - 1) / C; ch = (r + C - 1) / C;
	wet.assign((size_t)cw * ch, 0.0f); terr.assign((size_t)cw * ch, 0.0f);
	ow = (c * std::max(1, FIB_PX) + 3) / 4; oh = (r * std::max(1, FIB_PX) + 3) / 4;
	occ.assign((size_t)ow * oh, 0u);
	clear();
}

int MyceliumSim::activeThreads() const
{
	int n = 0;
	for (const Thread & t : threads) n += t.alive;
	return n;
}

void MyceliumSim::step(const Inputs & in)
{
	if (in.cols <= 0 || in.rows <= 0 || !in.sand)
		return;
	if (in.cols != cols || in.rows != rows)
		resize(in.cols, in.rows);
	mm = std::max(1e-3f, in.mmPerCell);
	float dt = in.dt;
	if (!(dt > 0.0f) || dt > 0.1f) dt = 1.0f / 40.0f;
	time += dt;
	stepNo++;
	auto t0 = std::chrono::steady_clock::now();
	auto lap = [&](int k) { auto t1 = std::chrono::steady_clock::now(); partTime[k] = std::chrono::duration<float, std::milli>(t1 - t0).count(); t0 = t1; };
	updateMask(in, dt); lap(0);
	updateLiquid(in, dt); lap(1);
	if (stepNo % 5 == 1) updateCoarse(); // page: read-back every 5 frames
	growThreads(in, dt); lap(2);
	if (touchedNow && time - lastBase > 0.4) dirtyBase = true; // page: rebuilt every 0.4 s while touching
}

// ------------------------------------------------------------ lit footprint (page fsMaskTouch, fsMaskAge)
static void box3(const float * src, float * dst, int W, int H)
{
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float s = 0; int n = 0;
			for (int dy = -1; dy <= 1; dy++) {
				const int yy = y + dy; if (yy < 0 || yy >= H) continue;
				for (int dx = -1; dx <= 1; dx++) {
					const int xx = x + dx; if (xx < 0 || xx >= W) continue;
					s += src[(size_t)yy * W + xx]; n++;
				}
			}
			dst[(size_t)y * W + x] = s / n;
		}
}

void MyceliumSim::updateMask(const Inputs & in, float dt)
{
	const int W = cols, H = rows;
	const size_t n = (size_t)W * H;
	if (in.footprint && !in.adopting) box3(in.footprint, fp2.data(), W, H);
	else std::fill(fp2.begin(), fp2.end(), 0.0f);
	const float fade = std::exp(-dt * fadeRate());
	touchedNow = false;
	for (size_t i = 0; i < n; i++) {
		R[i] = std::max(R[i] * fade, smoothstepf(0.25f, 0.55f, fp2[i]));
		touched[i] = fp2[i] > 0.3f;
		if (touched[i]) { G[i] = 0.0f; touchedNow = true; }
		else G[i] = std::min(G[i] + dt, AGE_MAX);
	}
	// Stroke direction: from older to newer touch, fitted over recent touches nearby.
	if (touchedNow) {
		const int rad = std::max(1, DIR_RADIUS_CELLS);
		const float win = std::max(0.05f, DIR_WINDOW_SEC);
		for (int y = 0; y < H; y++)
			for (int x = 0; x < W; x++) {
				const size_t i = (size_t)y * W + x;
				if (!touched[i]) continue;
				float sx = 0, sy = 0; int cnt = 0;
				for (int dy = -rad; dy <= rad; dy++) {
					const int yy = y + dy; if (yy < 0 || yy >= H) continue;
					for (int dx = -rad; dx <= rad; dx++) {
						const int xx = x + dx; if (xx < 0 || xx >= W || (dx == 0 && dy == 0)) continue;
						const float g = G[(size_t)yy * W + xx];
						if (g > win) continue;
						sx += dx * g; sy += dy * g; cnt++;
					}
				}
				// G grows toward older touches; the stroke runs the other way.
				const float l = std::sqrt(sx * sx + sy * sy);
				if (cnt < 4 || l < 0.02f * cnt) continue; // no clear gradient: keep the last good value
				const float ux = -sx / l, uy = -sy / l;
				const float w = std::min(1.0f, fp2[i] * 1.5f);
				float nx = DX[i] + (ux - DX[i]) * w, ny = DY[i] + (uy - DY[i]) * w;
				if (DX[i] == 0.0f && DY[i] == 0.0f) { nx = ux; ny = uy; }
				const float nl = std::sqrt(nx * nx + ny * ny) + 1e-5f;
				DX[i] = nx / nl; DY[i] = ny / nl;
			}
	}
	// Warming from the model's recovery (option): eased toward max(cover, S), reset by touch.
	if (WARM_FROM_MODEL) {
		const float ease = 1.0f - std::exp(-dt / std::max(0.05f, MODEL_EASE_SEC));
		for (size_t i = 0; i < n; i++) {
			if (touched[i]) { MW[i] = 0.0f; continue; }
			if (R[i] <= 0.0f) continue;
			float c = 0;
			for (int r = 0; r < 3; r++) if (in.cover[r]) c += in.cover[r][i];
			const float target = std::max(std::min(1.0f, c), in.soil ? in.soil[i] : 0.0f);
			if (target > MW[i]) MW[i] += (target - MW[i]) * ease;
		}
	}
	// Smoothed sand (5 x 5 box, every 5 steps): the only sand the look uses.
	if (stepNo % 5 == 1 || stepNo < 3) {
		box3(in.sand, tmp.data(), W, H);
		box3(tmp.data(), sandS.data(), W, H);
	}
}

// ------------------------------------------------------------ liquid motion (page fsFlux, fsDepth, fsVelL)
void MyceliumSim::updateLiquid(const Inputs & in, float dt)
{
	(void)in;
	const int LW = lw, LH = lh, W = cols, H = rows;
	const size_t ln = (size_t)LW * LH;
	for (int j = 0; j < LH; j++)
		for (int i = 0; i < LW; i++) {
			float t = 0, s = 0; int c = 0;
			for (int dy = 0; dy < 2; dy++)
				for (int dx = 0; dx < 2; dx++) {
					const int x = 2 * i + dx, y = 2 * j + dy;
					if (x >= W || y >= H) continue;
					t += sandS[(size_t)y * W + x]; s += fp2[(size_t)y * W + x]; c++;
				}
			const size_t l = (size_t)j * LW + i;
			lT[l] = t / std::max(1, c);
			lSrc[l] = s / std::max(1, c);
			lH[l] += std::max(0.0f, POUR) * dt * lSrc[l];
		}
	const float h = dt / LIQUID_SUBSTEPS;
	const float damp = std::exp(-h * std::max(0.0f, VISC));
	const float evap = EVAP_MEM >= NEVER ? 0.0f : std::log(10.0f) / std::max(1.0f, EVAP_MEM);
	const float evapF = std::exp(-h * evap);
	const float yld = std::max(0.0f, YIELD), K = std::max(0.0f, PIPE_K);
	auto S = [&](int i, int j) { const size_t l = (size_t)j * LW + i; return lT[l] + lH[l]; };
	for (int sub = 0; sub < LIQUID_SUBSTEPS; sub++) {
		// flux: 0 to x-1, 1 to x+1, 2 to y+1, 3 to y-1 (page x, y, z, w)
		for (int j = 0; j < LH; j++)
			for (int i = 0; i < LW; i++) {
				const size_t l = (size_t)j * LW + i;
				const float s = S(i, j), hh = lH[l];
				float d[4] = { i > 0 ? s - S(i - 1, j) : 0.0f, i < LW - 1 ? s - S(i + 1, j) : 0.0f,
					j < LH - 1 ? s - S(i, j + 1) : 0.0f, j > 0 ? s - S(i, j - 1) : 0.0f };
				float f[4], tot = 0;
				for (int k = 0; k < 4; k++) {
					const float dd = (d[k] > 0 ? 1.0f : (d[k] < 0 ? -1.0f : 0.0f)) * std::max(std::fabs(d[k]) - yld, 0.0f);
					f[k] = std::max(0.0f, lF[4 * l + k] * damp + h * K * dd);
				}
				if (i == 0) f[0] = 0;
				if (i == LW - 1) f[1] = 0;
				if (j == LH - 1) f[2] = 0;
				if (j == 0) f[3] = 0;
				tot = (f[0] + f[1] + f[2] + f[3]) * h;
				const float sc = tot > 1e-6f ? std::min(1.0f, hh / tot) : 0.0f;
				for (int k = 0; k < 4; k++) lFn[4 * l + k] = f[k] * sc;
			}
		lF.swap(lFn);
		for (int j = 0; j < LH; j++)
			for (int i = 0; i < LW; i++) {
				const size_t l = (size_t)j * LW + i;
				const float inL = i > 0 ? lF[4 * (l - 1) + 1] : 0.0f;
				const float inR = i < LW - 1 ? lF[4 * (l + 1) + 0] : 0.0f;
				const float inT = j < LH - 1 ? lF[4 * (l + LW) + 3] : 0.0f;
				const float inB = j > 0 ? lF[4 * (l - LW) + 2] : 0.0f;
				const float out = lF[4 * l] + lF[4 * l + 1] + lF[4 * l + 2] + lF[4 * l + 3];
				lHn[l] = std::max(lH[l] + h * ((inL + inR + inT + inB) - out), 0.0f) * evapF;
			}
		lH.swap(lHn);
	}
	for (int j = 0; j < LH; j++)
		for (int i = 0; i < LW; i++) {
			const size_t l = (size_t)j * LW + i;
			const float inL = i > 0 ? lF[4 * (l - 1) + 1] : 0.0f;
			const float inR = i < LW - 1 ? lF[4 * (l + 1) + 0] : 0.0f;
			const float inT = j < LH - 1 ? lF[4 * (l + LW) + 3] : 0.0f;
			const float inB = j > 0 ? lF[4 * (l - LW) + 2] : 0.0f;
			const float hh = std::max(lH[l], 0.3f);
			float vx = 0.5f * ((inL - lF[4 * l]) + (lF[4 * l + 1] - inR)) / hh;
			float vy = 0.5f * ((inB - lF[4 * l + 3]) + (lF[4 * l + 2] - inT)) / hh;
			const float m = std::sqrt(vx * vx + vy * vy);
			if (m > 40.0f) { vx *= 40.0f / m; vy *= 40.0f / m; }
			lVX[l] = vx; lVY[l] = vy;
		}
	(void)ln;
}

// ------------------------------------------------------------ coarse grid and edges (page readback)
void MyceliumSim::updateCoarse()
{
	const int C = std::max(1, COARSE_CELLS), W = cols, H = rows;
	for (int j = 0; j < ch; j++)
		for (int i = 0; i < cw; i++) {
			float w = 0, t = 0; int n = 0;
			for (int y = j * C; y < std::min(H, j * C + C); y++)
				for (int x = i * C; x < std::min(W, i * C + C); x++) { w += R[(size_t)y * W + x]; t += sandS[(size_t)y * W + x]; n++; }
			wet[(size_t)j * cw + i] = n ? std::min(1.0f, w / n) : 0.0f;
			terr[(size_t)j * cw + i] = n ? t / n : 0.0f;
		}
	edges.clear();
	for (int j = 2; j < ch - 2; j++)
		for (int i = 2; i < cw - 2; i++) {
			const int c = j * cw + i;
			// only the outer edge: lit here, and unlit sand a few cells further out
			const float gx = (wet[c + 2] - wet[c - 2]) / 4, gy = (wet[c + 2 * cw] - wet[c - 2 * cw]) / 4, gl = std::hypot(gx, gy);
			if (wet[c] < 0.15f || gl < 0.04f) continue;
			const int ox = (int)std::lround(i - 3 * gx / gl), oy = (int)std::lround(j - 3 * gy / gl);
			if (ox < 0 || oy < 0 || ox >= cw || oy >= ch || wet[(size_t)oy * cw + ox] < 0.05f) edges.push_back(c);
		}
}

float MyceliumSim::sampleCoarse(const std::vector<float> & a, float px, float py) const
{
	const float cpx = (float)std::max(1, COARSE_CELLS) * std::max(1, FIB_PX);
	const int i = std::min(cw - 1, std::max(0, (int)std::floor(px / cpx)));
	const int j = std::min(ch - 1, std::max(0, (int)std::floor(py / cpx)));
	return a[(size_t)j * cw + i];
}

// ------------------------------------------------------------ threads (page growThreads)
void MyceliumSim::emit(int ti, float x1, float y1, float s1)
{
	Thread & t = threads[ti];
	const float w = std::max(0.5f, WIDTH * std::pow(0.8f, (float)t.gen));
	if ((int)segs.size() >= std::max(1, MAX_SEGMENTS)) segs.pop_front();
	segs.push_back(Segment{ t.sx, t.sy, x1, y1, w, t.ss, s1, t.phase, (float)time, t.hue, nextSeg++ });
	t.sx = x1; t.sy = y1; t.ss = s1;
}

void MyceliumSim::growThreads(const Inputs & in, float dt)
{
	removed.clear();
	const float pxmm = fibPxPerMm();
	const float FWp = (float)cols * std::max(1, FIB_PX), FHp = (float)rows * std::max(1, FIB_PX);
	const float cpx = (float)std::max(1, COARSE_CELLS) * std::max(1, FIB_PX);
	auto newThread = [&](float x, float y, float a, int gen, uint32_t parent, float s0, float phase, float hue) {
		const uint32_t id = nextId++;
		if (parentOf.size() <= id) parentOf.resize(id + 1024, 0);
		parentOf[id] = parent;
		Thread t{ x, y, a, 0.0f, 0.0f, 0.0f, s0, 0.0f, phase, hue, x, y, s0, gen, id, true };
		threads.push_back(t);
	};
	auto related = [&](uint32_t a, uint32_t b) { return a == b || parentOf[a] == b || parentOf[b] == a; };
	if (THREADS_ON && !edges.empty()) {
		float expected = std::max(0.0f, SPAWN) * dt * edges.size() * (std::max(1, COARSE_CELLS) * mm) / 100.0f;
		while (expected > 0.0f) {
			if (rnd01() < std::min(1.0f, expected) && activeThreads() < MAX_THREADS) {
				const int c = edges[(size_t)(rnd01() * edges.size()) % edges.size()], i = c % cw, j = c / cw;
				const float gx = wet[c + 2] - wet[c - 2], gy = wet[c + 2 * cw] - wet[c - 2 * cw];
				newThread((i + 0.5f) * cpx, (j + 0.5f) * cpx, std::atan2(-gy, -gx) + (rnd01() - 0.5f) * 0.9f, 0, 0, 0.0f, rnd01(), rnd01());
			}
			expected -= 1.0f;
		}
	}
	const float kSigma = CURVE * PI_F / 180.0f / 10.0f; // per mm
	const float corr = std::max(0.1f, CORR);
	const size_t n0 = threads.size();
	for (size_t ti = 0; ti < n0; ti++) {
		if (!threads[ti].alive) continue;
		Thread & t = threads[ti];
		const float step = std::max(0.0f, SPEED) * dt;      // mm
		const float stepPx = step * pxmm;
		const float r = std::min(1.0f, step / corr);
		t.k += -t.k * r + kSigma * std::sqrt(2.0f * r) * randn();
		t.a += t.k * step;
		if (SLOPE > 0.0f) {
			const float o = 6.0f * pxmm;
			const float gx = sampleCoarse(terr, t.x + o, t.y) - sampleCoarse(terr, t.x - o, t.y);
			const float gy = sampleCoarse(terr, t.x, t.y + o) - sampleCoarse(terr, t.x, t.y - o);
			if (std::hypot(gx, gy) > 0.5f) {
				const float ca = std::atan2(gx, -gy);
				float d = std::atan2(std::sin(ca - t.a), std::cos(ca - t.a));
				if (std::fabs(d) > PI_F / 2) d = std::atan2(std::sin(ca + PI_F - t.a), std::cos(ca + PI_F - t.a));
				t.a += d * SLOPE * std::min(1.0f, step / 20.0f);
			}
		}
		const float nx = t.x + std::cos(t.a) * stepPx, ny = t.y + std::sin(t.a) * stepPx;
		t.age += dt; t.travel += step; t.sinceBranch += step;
		bool dead = nx < 2 || ny < 2 || nx > FWp - 2 || ny > FHp - 2 || t.age > TIP_LIFE;
		if (!dead && in.water) {
			const int cx = std::min(cols - 1, (int)(nx / std::max(1, FIB_PX))), cy = std::min(rows - 1, (int)(ny / std::max(1, FIB_PX)));
			dead = in.water[(size_t)cy * cols + cx] != 0; // the sandbox's own water
		}
		if (dead) {
			if (std::hypot(t.x - t.sx, t.y - t.sy) > 0.2f) emit((int)ti, t.x, t.y, t.s);
			threads[ti].alive = false;
			continue;
		}
		const int ocx = std::min(ow - 1, (int)(nx / 4)), ocy = std::min(oh - 1, (int)(ny / 4));
		const size_t oc = (size_t)ocy * ow + ocx;
		const uint32_t owner = occ[oc];
		const bool onLight = t.travel > 25.0f && sampleCoarse(wet, nx, ny) > 0.5f;
		const bool fuse = (owner && !related(owner, t.id) && t.travel > 15.0f) || onLight;
		t.s += stepPx;
		if (fuse) {
			if (onLight) stopsOnLight++; else fuses++;
			emit((int)ti, nx, ny, t.s);
			threads[ti].alive = false;
			continue;
		}
		occ[oc] = t.id;
		t.x = nx; t.y = ny;
		if (std::hypot(t.x - t.sx, t.y - t.sy) >= SEGMENT_MM * pxmm) emit((int)ti, t.x, t.y, t.s);
		if (rnd01() < BRANCH / 100.0f * step && t.sinceBranch > 8.0f) {
			Thread & u = threads[ti];
			u.sinceBranch = 0;
			const float a = u.a + (rnd01() < 0.5f ? -1.0f : 1.0f) * (0.5f + rnd01() * 0.55f);
			const float hue = std::min(1.0f, std::max(0.0f, u.hue + (rnd01() - 0.5f) * 0.3f));
			newThread(nx, ny, a, u.gen + 1, u.id, u.s, u.phase, hue); // may reallocate: t is not used after this
		}
	}
	if (threads.size() > 4000)
		threads.erase(std::remove_if(threads.begin(), threads.end(), [](const Thread & t) { return !t.alive; }), threads.end());
	// Persistence: drop segments only once faded (Trace memory), or beyond the cap.
	const float fr = fadeRate();
	if (fr > 0.0f)
		while (!segs.empty() && std::exp(-(float)(time - segs.front().birth) * fr) < 0.02f) segs.pop_front();
	if (in.water) {
		const int fp = std::max(1, FIB_PX);
		auto wetAt = [&](float x, float y) {
			const int cx = std::min(cols - 1, std::max(0, (int)(x / fp))), cy = std::min(rows - 1, std::max(0, (int)(y / fp)));
			return in.water[(size_t)cy * cols + cx] != 0;
		};
		for (Segment & s : segs)
			if (s.width > 0.0f && (wetAt(s.x0, s.y0) || wetAt(s.x1, s.y1))) { s.width = 0.0f; removed.push_back(s.id); }
	}
}
