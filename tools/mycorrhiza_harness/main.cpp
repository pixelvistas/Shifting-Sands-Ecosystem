// Headless check of Prototype 2 (Mycorrhiza), 2026-11-12.
// Compiles the real SuccessionModel.cpp and MycorrhizaField.cpp (no
// openFrameworks) and runs four checks on a 519 x 295 grid (1.9 mm/cell):
//
//   noise:   flat terrain with +-2 mm uniform per-cell noise every frame for
//            10 minutes -> agents spawned must be 0.
//   ident:   SuccessionModel run twice from bare over 2,000 steps with the
//            same inputs (strokes included), once alone and once with the
//            module stepping on its outputs -> cover, M and S bit-identical.
//   bench:   one MycorrhizaField step with 100,000 agents (module only).
//   story:   a mound with a water channel, bare at the start; the succession
//            model warms up for 240 s with nothing touched (nothing must be
//            drawn), then four simulated hand strokes (a moving disc that
//            digs the sand, with the hand occluding the depth samples under
//            it), then a long pause with the slow fade. Frames are drawn the
//            way the renderer's shader does (8-bit sqrt-encoded trace,
//            normalised by the field's display level, colour ramp, ring glow)
//            on black, as PNGs: story_NNNN.png.
//
// Usage: ./mycorrhiza_harness <outdir> [repoRoot] [noise|ident|bench|story ...]
#include "../../src/Games/SuccessionModel.h"
#include "../../src/Prototypes/Mycorrhiza/MycorrhizaField.h"
#include "../dewitz_harness/png.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

typedef SuccessionModel SM;
typedef MycorrhizaField MF;
static std::string OUT, ROOT = "../..";
static const float DT = 1.0f / 40.0f;
static const float MM = 1.9f;
static const float TABLE_MM = -15.0f;
static const int GW = 519, GH = 295;
static const float FLAT_MM = 9.0f;

// ------------------------------------------------------------ preset
static std::string tag(const std::string & block, const std::string & name)
{
	std::string open = "<" + name + ">", close = "</" + name + ">";
	size_t a = block.find(open), b = block.find(close);
	if (a == std::string::npos || b == std::string::npos) return "";
	return block.substr(a + open.size(), b - a - open.size());
}

static bool readPreset(const std::string & path, SM::RoleParams out[3])
{
	std::ifstream f(path);
	if (!f) return false;
	std::stringstream ss; ss << f.rdbuf();
	std::string doc = ss.str();
	const char * roles[3] = { "ROLE_PIONEER", "ROLE_INTERMEDIATE", "ROLE_CLIMAX" };
	const char * names[3] = { "pioneer", "intermediate", "climax" };
	for (int r = 0; r < 3; r++) {
		std::string b = tag(doc, roles[r]);
		if (b.empty()) return false;
		out[r].name = names[r];
		auto rng = [&](const char * p, SM::Range & x) {
			x.min = (float)atof(tag(b, std::string(p) + "MIN").c_str());
			x.max = (float)atof(tag(b, std::string(p) + "MAX").c_str());
			x.unit = tag(b, std::string(p) + "UNIT");
		};
		rng("MOISTURE", out[r].moisture);
		rng("SLOPE", out[r].slope);
		rng("SOILDEPTH", out[r].soilDepth);
	}
	return true;
}
static SM::RoleParams PRESET[3];

// ------------------------------------------------------------ display (copy of mycorrhiza.frag)
// Defaults of MycorrhizaRenderer.
static float RAMP_POS[5] = { 0.0f, 0.35f, 0.65f, 0.9f, 1.0f };
static const float RAMP[5][3] = {
	{ 0.10f, 0.04f, 0.32f },  // deep indigo
	{ 0.42f, 0.14f, 0.78f },  // violet
	{ 0.88f, 0.22f, 0.78f },  // magenta
	{ 0.30f, 0.86f, 1.00f },  // cyan
	{ 1.00f, 0.80f, 0.50f } };// warm highlight
static float GLOW_RADIUS = 2.0f;   // texels
static float GLOW_STRENGTH = 0.25f;
static const float BRIGHTNESS = 1.0f;
static float CORE_GAIN = 1.5f;
static float GAMMA = 0.9f;       // display gamma on the normalised trace

static void ramp(float t, float out[3])
{
	t = std::min(1.0f, std::max(0.0f, t));
	for (int s = 0; s < 4; s++)
		if (t <= RAMP_POS[s + 1] || s == 3) {
			float u = (t - RAMP_POS[s]) / std::max(1e-4f, RAMP_POS[s + 1] - RAMP_POS[s]);
			u = std::min(1.0f, std::max(0.0f, u));
			for (int k = 0; k < 3; k++) out[k] = RAMP[s][k] + (RAMP[s + 1][k] - RAMP[s][k]) * u;
			return;
		}
}

// 8-bit texture, as MycorrhizaRenderer uploads it: round(sqrt(clamp(trace / level)) x 255).
static void encode(const MF & f, std::vector<unsigned char> & tex)
{
	const float * t = f.trace();
	const float inv = 1.0f / f.getDisplayLevel();
	tex.resize((size_t)GW * GH);
	for (size_t i = 0; i < tex.size(); i++)
		tex[i] = (unsigned char)std::lround(std::sqrt(std::min(1.0f, std::max(0.0f, t[i] * inv))) * 255.0f);
}
// GL_LINEAR sample of the encoded texture at texel coordinate (x, y), clamped to edge, then decoded.
static float sampleDecoded(const std::vector<unsigned char> & tex, float x, float y)
{
	x -= 0.5f; y -= 0.5f;
	int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
	float tx = x - x0, ty = y - y0;
	auto at = [&](int xi, int yi) { xi = std::max(0, std::min(GW - 1, xi)); yi = std::max(0, std::min(GH - 1, yi)); return tex[(size_t)yi * GW + xi] / 255.0f; };
	float e = (at(x0, y0) * (1 - tx) + at(x0 + 1, y0) * tx) * (1 - ty) + (at(x0, y0 + 1) * (1 - tx) + at(x0 + 1, y0 + 1) * tx) * ty;
	return std::pow(e * e, GAMMA);
}
static std::vector<unsigned char> renderFrame(const MF & f)
{
	std::vector<unsigned char> tex; encode(f, tex);
	std::vector<unsigned char> img((size_t)GW * GH * 3);
	static const float DIRS[8][2] = { {1,0},{0.7071f,0.7071f},{0,1},{-0.7071f,0.7071f},{-1,0},{-0.7071f,-0.7071f},{0,-1},{0.7071f,-0.7071f} };
	for (int y = 0; y < GH; y++)
		for (int x = 0; x < GW; x++) {
			float cx = x + 0.5f, cy = y + 0.5f;
			float core = sampleDecoded(tex, cx, cy);
			float g = 0.0f;
			for (int d = 0; d < 8; d++) {
				g += sampleDecoded(tex, cx + DIRS[d][0] * GLOW_RADIUS, cy + DIRS[d][1] * GLOW_RADIUS);
				g += 0.5f * sampleDecoded(tex, cx + DIRS[d][0] * 2.0f * GLOW_RADIUS, cy + DIRS[d][1] * 2.0f * GLOW_RADIUS);
			}
			g /= 12.0f;
			float rc[3], rg[3]; ramp(core, rc); ramp(g, rg);
			for (int k = 0; k < 3; k++) {
				float v = (rc[k] * std::min(1.0f, core * CORE_GAIN) + GLOW_STRENGTH * rg[k] * g) * BRIGHTNESS;
				img[((size_t)y * GW + x) * 3 + k] = (unsigned char)std::lround(std::min(1.0f, v) * 255.0f);
			}
		}
	return img;
}

// ------------------------------------------------------------ world
struct Stroke { float t0, dur, x0, y0, x1, y1; };
struct World {
	std::vector<float> bed, dug, elev, above;
	std::vector<unsigned char> water;
	std::mt19937 rng{ 11 };
	float noiseMM = 1.0f;
	std::vector<Stroke> strokes;
	float handRadiusMM = 20.0f, digRateMM = 30.0f, maxDigMM = 10.0f, occludeRadiusMM = 35.0f;
	World() : bed(GW * GH, FLAT_MM), dug(GW * GH, 0.0f), elev(GW * GH), above(GW * GH), water(GW * GH, 0) {}

	void mound() {
		const float cx = 0.55f * GW, cy = 0.42f * GH;
		for (int y = 0; y < GH; y++) for (int x = 0; x < GW; x++) {
			float r = std::hypot(x - cx, (y - cy) * 1.15f) * MM;
			float h = FLAT_MM + 62.0f * std::exp(-r * r / (2.0f * 140.0f * 140.0f));
			// Ground falls toward the channel (low, damp ground near it).
			float yc = 0.80f * GH + 14.0f * std::sin(x / 55.0f);
			float dch = std::fabs(y - yc) * MM;
			h -= 14.0f * std::exp(-dch / 60.0f);
			if (std::fabs(y - yc) < 7.0f) { h = -22.0f; water[y * GW + x] = 1; }
			bed[y * GW + x] = h;
		}
	}
	// Hand position at time t (cells), or false.
	bool hand(float t, float & hx, float & hy) const {
		for (const Stroke & s : strokes)
			if (t >= s.t0 && t < s.t0 + s.dur) {
				float u = (t - s.t0) / s.dur;
				hx = s.x0 + (s.x1 - s.x0) * u; hy = s.y0 + (s.y1 - s.y0) * u;
				return true;
			}
		return false;
	}
	void sample(float t) {
		std::uniform_real_distribution<float> U(-noiseMM, noiseMM);
		float hx = 0, hy = 0;
		bool h = hand(t, hx, hy);
		if (h) { // the hand digs a trench under it
			int r = (int)std::ceil(handRadiusMM / MM);
			for (int y = std::max(0, (int)hy - r); y <= std::min(GH - 1, (int)hy + r); y++)
				for (int x = std::max(0, (int)hx - r); x <= std::min(GW - 1, (int)hx + r); x++) {
					float d = std::hypot(x - hx, y - hy) * MM;
					if (d > handRadiusMM) continue;
					size_t i = (size_t)y * GW + x;
					float target = maxDigMM * (1.0f - d / handRadiusMM);
					dug[i] = std::min(std::max(dug[i], target), dug[i] + digRateMM * DT);
				}
		}
		for (int i = 0; i < GW * GH; i++) {
			float b = bed[i] - dug[i];
			elev[i] = b + U(rng);
			above[i] = b - TABLE_MM;
		}
		if (h) { // the hand itself: depth samples under it rejected
			int r = (int)std::ceil(occludeRadiusMM / MM);
			for (int y = std::max(0, (int)hy - r); y <= std::min(GH - 1, (int)hy + r); y++)
				for (int x = std::max(0, (int)hx - r); x <= std::min(GW - 1, (int)hx + r); x++)
					if (std::hypot(x - hx, y - hy) * MM <= occludeRadiusMM) elev[(size_t)y * GW + x] = NAN;
		}
	}
	SM::Inputs smInputs() {
		SM::Inputs in;
		in.elevationMM = elev.data(); in.water = water.data(); in.heightAboveTableMM = above.data();
		in.cols = GW; in.rows = GH; in.mmPerCell = MM; in.dt = DT;
		in.temperature = 0.745f; in.restingTemperature = 0.745f; in.spreadRadiusMM = 40.0f;
		return in;
	}
	static MF::Inputs mfInputs(const World & w, const SM & m) {
		MF::Inputs in;
		in.elevationMM = w.elev.data();
		bool ok = m.getCols() == GW && m.getRows() == GH;
		for (int r = 0; r < 3; r++) in.cover[r] = ok ? m.cover(r) : nullptr;
		in.soil = ok ? m.soil() : nullptr;
		in.moisture = ok ? m.moisture() : nullptr;
		in.water = w.water.data();
		in.cols = GW; in.rows = GH; in.mmPerCell = MM; in.dt = DT;
		return in;
	}
};

static int litCells(const MF & f, float frac = 0.05f)
{
	int n = 0; const float thr = frac * f.getDisplayLevel();
	for (int i = 0; i < GW * GH; i++) if (f.trace()[i] > thr) n++;
	return n;
}

// Displayed brightness summed over the grid: sum of (min(1, trace / level))^GAMMA.
static double shownMass(const MF & f)
{
	double sum = 0.0; const float inv = 1.0f / f.getDisplayLevel();
	for (int i = 0; i < GW * GH; i++) sum += std::pow(std::min(1.0f, f.trace()[i] * inv), GAMMA);
	return sum;
}
// Strand alignment with the terrain contours on sloping ground: strands run
// across the trace gradient, contours across the elevation gradient, so the
// two run together where |cos(grad trace, grad bed)| is near 1. Weighted by
// |grad trace|; isotropic strands would give 2/pi = 0.637.
static double contourAlignment(const MF & f, const World & w, double & weightOut)
{
	double num = 0, den = 0;
	for (int y = 2; y < GH - 2; y++)
		for (int x = 2; x < GW - 2; x++) {
			const int i = y * GW + x;
			if (w.water[i]) continue;
			float ex = (w.bed[i + 1] - w.dug[i + 1] - w.bed[i - 1] + w.dug[i - 1]) / (2 * MM);
			float ey = (w.bed[i + GW] - w.dug[i + GW] - w.bed[i - GW] + w.dug[i - GW]) / (2 * MM);
			float eg = std::hypot(ex, ey);
			if (eg < 0.1f) continue; // sloping ground only (> ~6 degrees)
			const float * t = f.trace();
			float tx = (t[i + 1] - t[i - 1]) * 0.5f, ty = (t[i + GW] - t[i - GW]) * 0.5f;
			float tg = std::hypot(tx, ty);
			if (tg < 1e-4f) continue;
			num += std::fabs(tx * ex + ty * ey) / eg; // |cos| x |grad trace|
			den += tg;
		}
	weightOut = den;
	return den > 0 ? num / den : 0.0;
}

// ------------------------------------------------------------ checks
static bool checkNoise()
{
	World w; w.noiseMM = 2.0f;
	MF f; f.clear();
	float worst = 0.0f; int touched = 0;
	const int steps = 10 * 60 * 40;
	for (int s = 0; s < steps; s++) {
		w.sample(s * DT);
		MF::Inputs in; in.elevationMM = w.elev.data(); in.cols = GW; in.rows = GH; in.mmPerCell = MM; in.dt = DT;
		f.step(in);
		worst = std::max(worst, f.maxTouchChangeMM());
		touched += f.touchedBlocks();
	}
	printf("noise: +-2 mm uniform per cell per frame, flat, 10 min (%d steps): agents spawned %lld, touched block-steps %d, "
		"largest block change %.3f mm (threshold %.1f mm)\n", steps, f.spawnedTotal(), touched, worst, MF::TOUCH_MM);
	return f.spawnedTotal() == 0;
}

static uint64_t fnv(const void * p, size_t n, uint64_t h = 1469598103934665603ull)
{
	const unsigned char * c = (const unsigned char *)p;
	for (size_t i = 0; i < n; i++) { h ^= c[i]; h *= 1099511628211ull; }
	return h;
}
static uint64_t hashModel(const SM & m)
{
	const size_t n = (size_t)GW * GH * sizeof(float);
	uint64_t h = fnv(m.cover(0), n); h = fnv(m.cover(1), n, h); h = fnv(m.cover(2), n, h);
	h = fnv(m.moisture(), n, h); return fnv(m.soil(), n, h);
}
static bool checkIdentical()
{
	std::string err;
	World wa, wb; wa.mound(); wb.mound();
	std::vector<Stroke> st = { { 5, 3, 120, 120, 220, 90 }, { 20, 3, 300, 200, 380, 150 } };
	wa.strokes = wb.strokes = st;
	SM a, b; a.setParams(PRESET, err); b.setParams(PRESET, err);
	MF f; f.clear();
	bool same = true; int firstDiff = -1;
	for (int s = 0; s < 2000; s++) {
		wa.sample(s * DT); wb.sample(s * DT);
		SM::Inputs ia = wa.smInputs(), ib = wb.smInputs();
		a.step(ia); b.step(ib);
		f.step(World::mfInputs(wb, b)); // module on run b only
		if (s % 100 == 99 || s == 1999) {
			if (hashModel(a) != hashModel(b)) { same = false; if (firstDiff < 0) firstDiff = s; }
		}
	}
	const size_t n = (size_t)GW * GH * sizeof(float);
	bool bytes = std::memcmp(a.cover(0), b.cover(0), n) == 0 && std::memcmp(a.cover(1), b.cover(1), n) == 0 &&
		std::memcmp(a.cover(2), b.cover(2), n) == 0 && std::memcmp(a.moisture(), b.moisture(), n) == 0 &&
		std::memcmp(a.soil(), b.soil(), n) == 0;
	printf("ident: 2,000 steps, mound + channel + 2 strokes; module agents at end %d (spawned %lld); cover/M/S hash "
		"alone %016llx, with module %016llx; bit-identical at every 100-step check: %s; final memcmp: %s\n",
		f.agentCount(), f.spawnedTotal(), (unsigned long long)hashModel(a), (unsigned long long)hashModel(b),
		same ? "yes" : "NO", bytes ? "identical" : "DIFFERENT");
	(void)firstDiff;
	return same && bytes;
}

static void bench(int threads)
{
	const int savedThreads = MF::THREADS; MF::THREADS = threads;
	// Fill to 100,000 agents: touch everywhere by moving the whole surface.
	World w; w.noiseMM = 1.0f;
	MF f;
	const int savedCap = MF::MAX_AGENTS; MF::MAX_AGENTS = 100000;
	std::vector<float> ms; double acc[7] = {0};
	for (int s = 0; s < 1200; s++) {
		float t = s * DT;
		w.sample(t);
		float lift = s < 200 ? 15.0f * std::sin(t * 6.0f) : 0.0f; // 5 s of whole-box "touch"
		for (float & e : w.elev) e += lift;
		MF::Inputs in; in.elevationMM = w.elev.data(); in.cols = GW; in.rows = GH; in.mmPerCell = MM; in.dt = DT;
		f.step(in);
		if (f.agentCount() >= 100000) { ms.push_back(f.getLastStepMs()); for (int i = 0; i < 7; i++) acc[i] += f.getPhaseMs(i); }
	}
	std::sort(ms.begin(), ms.end());
	if (ms.empty()) { printf("bench: never reached 100,000 agents\n"); return; }
	double sum = 0; for (float v : ms) sum += v;
	printf("bench: 519 x 295, 100,000 agents (module only, THREADS = %d, -O2): %zu steps timed, mean %.2f ms, median %.2f ms, "
		"p95 %.2f ms, worst %.2f ms\n", threads, ms.size(), sum / ms.size(), ms[ms.size() / 2], ms[(size_t)(0.95 * (ms.size() - 1))], ms.back());
	(void)0;
	const char * names[7] = { "touch", "comb field", "spawn+sort", "agents", "trail", "trace+maps", "level" };
	printf("  by phase:");
	for (int i = 0; i < 7; i++) printf(" %s %.2f", names[i], acc[i] / ms.size());
	printf(" ms\n");
	MF::MAX_AGENTS = savedCap; MF::THREADS = savedThreads;
}

static void savePNG(const std::string & name, const std::vector<unsigned char> & img)
{
	png::writeRGB(OUT + "/" + name, GW, GH, img);
}

static void story(bool quick)
{
	std::string err;
	World w; w.mound();
	SM m; m.setParams(PRESET, err);
	MF f; f.clear();
	const float WARM = quick ? 20.0f : 240.0f;
	// Strokes (cells): mound flank, near the damp channel bank, far side, a joining stroke.
	w.strokes = {
		{ WARM + 0.0f, 3.0f, 150, 70, 230, 150 },
		{ WARM + 40.0f, 3.0f, 300, 200, 420, 190 },
		{ WARM + 90.0f, 3.0f, 400, 60, 460, 130 },
		{ WARM + 150.0f, 3.0f, 230, 150, 330, 195 } };
	const float END = WARM + 150.0f + 3.0f + (quick ? 40.0f : 660.0f);
	const int steps = (int)(END / DT);
	int frame = 0; int maxAgentsPreTouch = 0;
	float worstMs = 0.0f; double sumMs = 0; int nMs = 0; int peakAgents = 0;
	int lit1End = 0, lit1Plus10 = 0, lit1Plus30 = 0;
	double traceOnWater = 0.0, traceTotal = 0.0;
	double shownPause = 0, shown2 = 0, shown5 = 0, shown10 = 0, align = 0, alignW = 0;
	float levelPause = 0;
	for (int s = 0; s < steps; s++) {
		float t = s * DT;
		w.sample(t);
		SM::Inputs si = w.smInputs();
		m.step(si);
		f.step(World::mfInputs(w, m));
		if (t < WARM) maxAgentsPreTouch = std::max(maxAgentsPreTouch, f.agentCount());
		if (t >= WARM) { worstMs = std::max(worstMs, f.getLastStepMs()); sumMs += f.getLastStepMs(); nMs++; }
		peakAgents = std::max(peakAgents, f.agentCount());
		auto at = [&](float tt) { return s == (int)std::lround(tt / DT); };
		if (at(WARM + 3.0f)) lit1End = litCells(f);
		if (at(WARM + 13.0f)) lit1Plus10 = litCells(f);
		if (at(WARM + 33.0f)) lit1Plus30 = litCells(f);
		const float PAUSE = WARM + 153.0f + 30.0f; // agents from the last stroke mostly gone
		if (at(PAUSE)) { shownPause = shownMass(f); levelPause = f.getDisplayLevel(); align = contourAlignment(f, w, alignW); }
		if (at(PAUSE + 120.0f)) shown2 = shownMass(f);
		if (at(PAUSE + 300.0f)) shown5 = shownMass(f);
		if (at(PAUSE + 600.0f)) shown10 = shownMass(f);
		// Frames: every 30 s before touch, every 1 s for 200 s, then every 10 s.
		bool shoot = quick ? (s % 160 == 0) : (t < WARM && s % (30 * 40) == 0) || (t >= WARM && t < WARM + 200.0f && s % 40 == 0) ||
			(t >= WARM + 200.0f && s % 400 == 0);
		if (shoot) {
			char name[64]; snprintf(name, sizeof name, "story_%04d.png", frame++);
			auto img = renderFrame(f);
			if (t < WARM) { // must be pure black
				bool black = true; for (unsigned char c : img) if (c) { black = false; break; }
				if (!black) printf("story: FAIL non-black frame at t = %.0f s before any touch\n", t);
			}
			if (at(WARM + 190.0f)) {
				savePNG("still.png", img);
				// Annotated copy for the reader: the (invisible) water in dark grey.
				auto ann = img;
				for (int i = 0; i < GW * GH; i++)
					if (w.water[i] && !ann[3 * i] && !ann[3 * i + 1] && !ann[3 * i + 2]) ann[3 * i] = ann[3 * i + 1] = ann[3 * i + 2] = 40;
				savePNG("still_annotated.png", ann);
			}
			// GIF annotation only (not projected): a grey ring where the simulated hand is.
			float hx, hy;
			if (w.hand(t, hx, hy)) {
				const float r = w.handRadiusMM / MM;
				for (int a = 0; a < 360; a++) {
					int px = (int)std::lround(hx + r * std::cos(a * 0.01745f)), py = (int)std::lround(hy + r * std::sin(a * 0.01745f));
					if (px < 0 || py < 0 || px >= GW || py >= GH) continue;
					unsigned char * q = &img[((size_t)py * GW + px) * 3];
					q[0] = q[1] = q[2] = 160;
				}
			}
			savePNG(name, img);
		}
	}
	for (int i = 0; i < GW * GH; i++) { traceTotal += f.trace()[i]; if (w.water[i]) traceOnWater += f.trace()[i]; }
	// Trace on water at the busiest moment is what matters; re-measure is cheap here at the end.
	printf("story: %d steps (%.0f s); warm-up %.0f s untouched: max agents %d, frames black (checked above)\n", steps, END, WARM, maxAgentsPreTouch);
	printf("story: stroke 1 lit cells (> 5%% of level): end of stroke %d, +10 s %d, +30 s %d (keeps growing after the hand leaves)\n",
		lit1End, lit1Plus10, lit1Plus30);
	printf("story: peak agents %d; module step after first touch: mean %.2f ms, worst %.2f ms (with SuccessionModel inputs)\n",
		peakAgents, sumMs / std::max(1, nMs), worstMs);
	if (!quick) printf("story: displayed brightness (sum over cells) 30 s after the last stroke %.0f, +2 min %.0f (%.0f %%), +5 min %.0f (%.0f %%), "
		"+10 min %.0f (%.0f %%); display level then %.1f\n", shownPause, shown2, 100 * shown2 / std::max(1.0, shownPause),
		shown5, 100 * shown5 / std::max(1.0, shownPause), shown10, 100 * shown10 / std::max(1.0, shownPause), levelPause);
	printf("story: strand/contour alignment on sloping ground (|cos| weighted by trace gradient, isotropic = 0.637): %.3f\n", align);
	printf("story: trace on water cells at the end: %.3g of %.3g (water is never entered)\n", traceOnWater, traceTotal);
}

// KEY=value overrides of the field's tunables and the display copy.
static bool setParam(const std::string & kv)
{
	size_t e = kv.find('=');
	if (e == std::string::npos) return false;
	std::string k = kv.substr(0, e); float v = (float)atof(kv.c_str() + e + 1);
	struct F { const char * n; float * p; } fs[] = {
		{"TOUCH_MM",&MF::TOUCH_MM},{"TOUCH_MEMORY_SEC",&MF::TOUCH_MEMORY_SEC},{"SPAWN_PER_TOUCH",&MF::SPAWN_PER_TOUCH},
		{"SENSOR_ANGLE_DEG",&MF::SENSOR_ANGLE_DEG},{"SENSOR_DIST_MM",&MF::SENSOR_DIST_MM},{"ROTATE_DEG",&MF::ROTATE_DEG},
		{"STEP_MM",&MF::STEP_MM},{"MIN_MOBILITY",&MF::MIN_MOBILITY},{"COMB_WEIGHT",&MF::COMB_WEIGHT},
		{"COMB_SLOPE_SAT",&MF::COMB_SLOPE_SAT},{"CURL_SHARE",&MF::CURL_SHARE},{"CURL_SCALE_MM",&MF::CURL_SCALE_MM},
		{"AGENT_DEPOSIT",&MF::AGENT_DEPOSIT},{"TRACE_DEPOSIT",&MF::TRACE_DEPOSIT},{"TRACE_SENSE",&MF::TRACE_SENSE},
		{"TRACE_SATURATION",&MF::TRACE_SATURATION},{"FOOD_DEPOSIT",&MF::FOOD_DEPOSIT},{"TOUCH_DEPOSIT",&MF::TOUCH_DEPOSIT},
		{"TRAIL_DECAY",&MF::TRAIL_DECAY},{"TRACE_MEMORY_MIN",&MF::TRACE_MEMORY_MIN},{"AGENT_LIFE_SEC",&MF::AGENT_LIFE_SEC},
		{"LEVEL_PERCENTILE",&MF::LEVEL_PERCENTILE},{"LEVEL_FLOOR",&MF::LEVEL_FLOOR},
		{"CORE_GAIN",&CORE_GAIN},{"GAMMA",&GAMMA},{"GLOW_STRENGTH",&GLOW_STRENGTH},{"GLOW_RADIUS",&GLOW_RADIUS},
		{"RAMP1",&RAMP_POS[1]},{"RAMP2",&RAMP_POS[2]},{"RAMP3",&RAMP_POS[3]} };
	for (F & f : fs) if (k == f.n) { *f.p = v; printf("set %s = %g\n", f.n, v); return true; }
	if (k == "MAX_AGENTS") { MF::MAX_AGENTS = (int)v; return true; }
	if (k == "THREADS") { MF::THREADS = (int)v; return true; }
	fprintf(stderr, "unknown parameter %s\n", k.c_str());
	return false;
}

int main(int argc, char ** argv)
{
	if (argc < 2) { fprintf(stderr, "usage: %s <outdir> [repoRoot] [noise|ident|bench|story ...]\n", argv[0]); return 1; }
	OUT = argv[1];
	if (argc > 2) ROOT = argv[2];
	if (!readPreset(ROOT + "/bin/data/settings/ecologyParamsDesigner.xml", PRESET)) { fprintf(stderr, "no preset\n"); return 1; }
	std::vector<std::string> which;
	for (int i = 3; i < argc; i++) { std::string a = argv[i]; if (a.find('=') != std::string::npos) setParam(a); else which.push_back(a); }
	auto want = [&](const char * k) { return which.empty() || std::find(which.begin(), which.end(), k) != which.end(); };
	bool ok = true;
	if (want("noise")) ok = checkNoise() && ok;
	if (want("ident")) ok = checkIdentical() && ok;
	if (want("bench")) { bench(1); bench(4); }
	if (want("story")) story(false);
	if (std::find(which.begin(), which.end(), "quick") != which.end()) story(true);
	return ok ? 0 : 2;
}
