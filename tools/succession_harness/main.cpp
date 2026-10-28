// Headless harness for SuccessionModel (tests T1-T7, docs/DEVLOG.md
// 2026-10-28). Compiles the real src/Games/SuccessionModel.cpp; reads the
// real preset files with a small flat-tag reader (EcologyParams itself
// needs openFrameworks' ofXml). Writes, per test, a CSV (cover per role,
// mean moisture and soil over time) and a PNG time series (top row:
// vegetation, dominant role over grey at opacity = cover, water blue;
// middle: soil S, white to brown; bottom: moisture M, white to blue).
//
// Usage: ./succession_harness <outdir> [repoRoot]
#include "../../src/Games/SuccessionModel.h"
#include "../dewitz_harness/png.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <random>
#include <sstream>
#include <string>
#include <vector>

typedef SuccessionModel SM;
static std::string OUT, ROOT = "../..";
static const float DT = 1.0f / 40.0f;          // 40 fps, the sandbox's Release frame rate
static const float MM_PER_CELL = 1.9f;          // measured on the box
static const float TABLE_MM = -15.0f;           // VegetationField::WATER_TABLE_MM default

// ---------------------------------------------------------------- presets
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

// ---------------------------------------------------------------- world
struct World {
	int W, H;
	std::vector<float> bed;            // mm, true terrain
	std::vector<float> noisy;          // what the model is given this frame
	std::vector<float> above;          // bed above the table, mm
	std::vector<unsigned char> water;
	float noiseMM = 1.0f;              // uniform +/- per frame
	std::mt19937 rng{ 7 };
	World(int w, int h, float level) : W(w), H(h), bed(w * h, level), noisy(w * h), above(w * h), water(w * h, 0) {}
	void frame(SM & m, float temperature = 0.745f) {
		std::uniform_real_distribution<float> U(-noiseMM, noiseMM);
		for (int i = 0; i < W * H; i++) {
			noisy[i] = bed[i] + (noiseMM > 0 ? U(rng) : 0.0f);
			above[i] = bed[i] - TABLE_MM;
		}
		SM::Inputs in;
		in.elevationMM = noisy.data(); in.water = water.data(); in.heightAboveTableMM = above.data();
		in.cols = W; in.rows = H; in.mmPerCell = MM_PER_CELL; in.dt = DT;
		in.temperature = temperature; in.restingTemperature = 0.745f;
		in.spreadRadiusMM = 40.0f;
		m.step(in);
	}
};

struct Region { int x0, y0, x1, y1; };          // inclusive-exclusive
struct Sample { float t, v[3], m, s; };

static Sample sample(const SM & m, float t, const Region & r)
{
	Sample s{ t, { 0, 0, 0 }, 0, 0 };
	int n = 0;
	for (int y = r.y0; y < r.y1; y++)
		for (int x = r.x0; x < r.x1; x++) {
			int i = y * m.getCols() + x;
			for (int k = 0; k < 3; k++) s.v[k] += m.cover(k)[i];
			s.m += m.moisture()[i]; s.s += m.soil()[i]; n++;
		}
	for (int k = 0; k < 3; k++) s.v[k] /= n;
	s.m /= n; s.s /= n;
	return s;
}

static void writeCsv(const std::string & name, const std::vector<Sample> & rows)
{
	FILE * f = fopen((OUT + "/" + name + ".csv").c_str(), "w");
	fprintf(f, "t_s,pioneer,intermediate,climax,mean_moisture,mean_soil\n");
	for (auto & r : rows)
		fprintf(f, "%.2f,%.4f,%.4f,%.4f,%.4f,%.4f\n", r.t, r.v[0], r.v[1], r.v[2], r.m, r.s);
	fclose(f);
}

// Time-series image: one column of three panels per snapshot.
struct Strip {
	int W, H;
	Strip(int w, int h) : W(w), H(h) {}
	std::vector<std::vector<unsigned char>> shots;
	std::vector<float> times;
	void add(const SM & m, const World & w, float t) {
		std::vector<unsigned char> img(W * H * 3 * 3);
		const unsigned char role[3][3] = { { 230, 90, 20 }, { 0, 172, 172 }, { 0, 168, 89 } };
		for (int y = 0; y < H; y++)
			for (int x = 0; x < W; x++) {
				int i = y * W + x;
				float v0 = m.cover(0)[i], v1 = m.cover(1)[i], v2 = m.cover(2)[i];
				float tot = std::min(1.0f, v0 + v1 + v2);
				int d = (v0 >= v1 && v0 >= v2) ? 0 : (v1 >= v2 ? 1 : 2);
				float g = 110 + std::max(-60.0f, std::min(60.0f, w.bed[i])) ; // grey terrain
				unsigned char * p = &img[(y * W + x) * 3];
				for (int c = 0; c < 3; c++) p[c] = (unsigned char)(g * (1 - tot) + role[d][c] * tot);
				if (w.water[i]) { p[0] = 30; p[1] = 90; p[2] = 200; }
				float s = m.soil()[i];
				unsigned char * q = &img[((H + y) * W + x) * 3];
				q[0] = (unsigned char)(255 - s * (255 - 110)); q[1] = (unsigned char)(255 - s * (255 - 70)); q[2] = (unsigned char)(255 - s * (255 - 35));
				float mo = m.moisture()[i];
				unsigned char * r = &img[((2 * H + y) * W + x) * 3];
				r[0] = (unsigned char)(255 - mo * 225); r[1] = (unsigned char)(255 - mo * 160); r[2] = 255;
			}
		shots.push_back(img); times.push_back(t);
	}
	void save(const std::string & name) {
		int gap = 4, n = (int)shots.size();
		int TW = n * W + (n - 1) * gap, TH = 3 * H;
		std::vector<unsigned char> out(TW * TH * 3, 255);
		for (int k = 0; k < n; k++)
			for (int y = 0; y < TH; y++)
				for (int x = 0; x < W; x++)
					for (int c = 0; c < 3; c++)
						out[(y * TW + k * (W + gap) + x) * 3 + c] = shots[k][(y * W + x) * 3 + c];
		png::writeRGB(OUT + "/" + name + ".png", TW, TH, out);
		FILE * f = fopen((OUT + "/" + name + "_times.txt").c_str(), "w");
		for (float t : times) fprintf(f, "%.0f s\n", t);
		fclose(f);
	}
};

static int dominant(const Sample & s) { return (s.v[0] >= s.v[1] && s.v[0] >= s.v[2]) ? 0 : (s.v[1] >= s.v[2] ? 1 : 2); }
static const char * ROLE[3] = { "pioneer", "intermediate", "climax" };

// Runs flat moist ground from S = 0, recording the sequence.
struct SeqResult { float peakT[3], peakV[3], visibleT, domStart[3]; std::vector<Sample> rows; };
static SeqResult runSequence(SM & m, World & w, float seconds, const std::string & name, std::vector<float> shotTimes)
{
	SeqResult r;
	for (int k = 0; k < 3; k++) { r.peakT[k] = -1; r.peakV[k] = 0; r.domStart[k] = -1; }
	r.visibleT = -1;
	Region all{ 0, 0, w.W, w.H };
	Strip strip(w.W, w.H);
	int steps = (int)(seconds / DT), every = (int)(1.0f / DT);
	size_t nextShot = 0;
	for (int s = 1; s <= steps; s++) {
		w.frame(m);
		float t = s * DT;
		if (s % every == 0) {
			Sample q = sample(m, t, all);
			r.rows.push_back(q);
			for (int k = 0; k < 3; k++) if (q.v[k] > r.peakV[k]) { r.peakV[k] = q.v[k]; r.peakT[k] = t; }
			if (r.visibleT < 0 && q.v[0] > 0.3f) r.visibleT = t;
			int d = dominant(q);
			if (q.v[d] > 0.05f && r.domStart[d] < 0) r.domStart[d] = t;
		}
		if (nextShot < shotTimes.size() && t >= shotTimes[nextShot] - DT / 2) { strip.add(m, w, t); nextShot++; }
	}
	writeCsv(name, r.rows);
	strip.save(name);
	return r;
}

int main(int argc, char ** argv)
{
	if (argc < 2) { fprintf(stderr, "usage: %s <outdir> [repoRoot]\n", argv[0]); return 1; }
	OUT = argv[1];
	if (argc > 2) ROOT = argv[2];
	const char * only = getenv("SUCC_ONLY");
	auto want = [&](const char * t) { return !only || std::string(only).find(t) != std::string::npos; };

	SM::RoleParams designer[3], published[3], alt[3], unknownUnit[3];
	if (!readPreset(ROOT + "/bin/data/settings/ecologyParamsDesigner.xml", designer)) { fprintf(stderr, "cannot read designer preset\n"); return 1; }
	readPreset(ROOT + "/bin/data/settings/ecologyParamsPublished.xml", published);
	readPreset(ROOT + "/tools/succession_harness/presets/alt_soil.xml", alt);
	readPreset(ROOT + "/tools/succession_harness/presets/unknown_unit.xml", unknownUnit);
	std::string err;
	const int GW = 160, GH = 100;                    // small grid for T1-T5 (same per-cell physics)
	const float FLAT_MM = 0.0f;                      // flat sand at 0 mm: 15 mm above the table
	std::vector<float> shots = { 10, 20, 45, 90, 150, 240, 360 };

	SeqResult t1;
	if (want("T1") || want("T3")) {
		SM m; m.setParams(designer, err);
		World w(GW, GH, FLAT_MM);
		t1 = runSequence(m, w, 360, "T1_primary_sequence", shots);
		printf("T1 primary sequence (flat moist ground, S=0, designer preset):\n");
		for (int k = 0; k < 3; k++)
			printf("   %-12s peak cover %.2f at %5.0f s; dominant from %5.0f s\n", ROLE[k], t1.peakV[k], t1.peakT[k], t1.domStart[k]);
		printf("   pioneers visible (mean cover > 0.3) at %.0f s; final S %.2f, M %.2f\n", t1.visibleT, t1.rows.back().s, t1.rows.back().m);
		bool order = t1.peakT[0] < t1.peakT[1] && t1.peakT[1] < t1.peakT[2];
		printf("   peak order pioneer < intermediate < climax: %s\n", order ? "PASS" : "FAIL");

		if (want("T3")) {
			// Continue from the climax state: disturb two patches by raising
			// the sand 10 mm (identical moisture change). Patch A keeps
			// SOIL_RETAINED_FRACTION of its soil (the model's own rule);
			// patch B is new fill, S set to 0 directly (as specified).
			Region A{ 20, 30, 60, 70 }, B{ 100, 30, 140, 70 };
			for (const Region & r : { A, B })
				for (int y = r.y0; y < r.y1; y++) for (int x = r.x0; x < r.x1; x++) w.bed[y * GW + x] += 10.0f;
			Strip strip(GW, GH);
			std::vector<Sample> ra, rb;
			float tA = -1, tB = -1, tAL = -1, tBL = -1, t0 = 360, sA0 = -1, sB0 = -1;
			long long ev0 = m.getDisturbanceEvents();
			std::vector<float> s3 = { 2, 30, 60, 120, 240, 420 };
			size_t ns = 0;
			for (int s = 1; s <= (int)(420 / DT); s++) {
				w.frame(m);
				float t = s * DT;
				if (s == 2) { // the fill has registered as a disturbance by now; B is new material
					for (int y = B.y0; y < B.y1; y++) for (int x = B.x0; x < B.x1; x++) m.soilMutable()[y * GW + x] = 0.0f;
				}
				if (s % 40 == 0) {
					Sample a = sample(m, t, A), b = sample(m, t, B);
					if (sA0 < 0) { sA0 = a.s; sB0 = b.s; }
					ra.push_back(a); rb.push_back(b);
					if (tA < 0 && a.v[1] >= 0.5f) tA = t;
					if (tB < 0 && b.v[1] >= 0.5f) tB = t;
					if (tAL < 0 && a.v[1] + a.v[2] >= 0.5f) tAL = t;
					if (tBL < 0 && b.v[1] + b.v[2] >= 0.5f) tBL = t;
				}
				if (ns < s3.size() && t >= s3[ns] - DT / 2) { strip.add(m, w, t0 + t); ns++; }
			}
			writeCsv("T3_patchA_secondary", ra);
			writeCsv("T3_patchB_primary", rb);
			strip.save("T3_secondary_vs_primary");
			printf("T3 secondary vs primary (patches raised 10 mm at t=360 s): %lld disturbance events in the patches' cells\n",
				m.getDisturbanceEvents() - ev0);
			printf("   patch A (old soil, S %.2f after the event): intermediate >= 0.5 after %.0f s; intermediate+climax >= 0.5 after %.0f s\n", sA0, tA, tAL);
			printf("   patch B (new fill, S %.2f):                 intermediate >= 0.5 after %.0f s; intermediate+climax >= 0.5 after %.0f s\n", sB0, tB, tBL);
			printf("   literal criterion (intermediate cover, A faster): %s; intermediate-or-later cover, A faster: %s\n",
				(tA > 0 && (tB < 0 || tA < tB)) ? "PASS" : "FAIL (-1 = never within 420 s)", (tAL > 0 && (tBL < 0 || tAL < tBL)) ? "PASS" : "FAIL");
			for (size_t k = 0; k < ra.size(); k += 30)
				printf("      t+%3.0f s  A: p %.2f i %.2f c %.2f S %.2f M %.2f | B: p %.2f i %.2f c %.2f S %.2f M %.2f\n", ra[k].t,
					ra[k].v[0], ra[k].v[1], ra[k].v[2], ra[k].s, ra[k].m, rb[k].v[0], rb[k].v[1], rb[k].v[2], rb[k].s, rb[k].m);
		}
	}

	if (want("T2")) {
		float saved = SM::SOIL_BUILD_RATE;
		SM::SOIL_BUILD_RATE = 0.0f;
		SM m; m.setParams(designer, err);
		World w(GW, GH, FLAT_MM);
		SeqResult r = runSequence(m, w, 600, "T2_no_feedback", { 20, 120, 300, 600 });
		SM::SOIL_BUILD_RATE = saved;
		Sample f = r.rows.back();
		printf("T2 no feedback (SOIL_BUILD_RATE = 0), 600 s: pioneer %.2f, intermediate %.3f, climax %.3f, S %.3f -> %s\n",
			f.v[0], f.v[1], f.v[2], f.s, (f.v[0] > 0.5f && f.v[1] < 0.01f && f.v[2] < 0.01f) ? "PASS" : "FAIL");
	}

	if (want("T4")) {
		// Dry plateau 40 mm up (55 mm above the table); water crosses a
		// strip from t=10 s to t=40 s.
		SM m; m.setParams(designer, err);
		World w(GW, GH, 40.0f);
		Region strip{ 0, 40, GW, 60 }, dry{ 0, 0, GW, 20 };
		Strip img(GW, GH);
		std::vector<Sample> rows;
		std::vector<float> s4 = { 5, 30, 45, 70, 100, 150, 200, 300 };
		size_t ns = 0;
		for (int s = 1; s <= (int)(300 / DT); s++) {
			float t = s * DT;
			bool on = t >= 10.0f && t < 40.0f;
			for (int y = strip.y0; y < strip.y1; y++) for (int x = 0; x < GW; x++) w.water[y * GW + x] = on ? 1 : 0;
			w.frame(m);
			if (s % 40 == 0) rows.push_back(sample(m, t, strip));
			if (ns < s4.size() && t >= s4[ns] - DT / 2) { img.add(m, w, t); ns++; }
		}
		float mAtStop = 0, tHalf = -1, peak = 0, tPeak = -1, tGone = -1, tGrow = -1;
		for (auto & q : rows) if (std::fabs(q.t - 40.0f) < 0.5f) mAtStop = q.m;
		for (auto & q : rows) if (q.t > 40 && tHalf < 0 && q.m < 0.5f * mAtStop) tHalf = q.t - 40;
		for (auto & q : rows) { float tot = q.v[0] + q.v[1] + q.v[2]; if (tot > peak) { peak = tot; tPeak = q.t; } }
		float tLastHalf = -1;
		for (auto & q : rows) {
			float tot = q.v[0] + q.v[1] + q.v[2];
			if (tGrow < 0 && tot > 0.1f) tGrow = q.t;
			if (tot >= 0.5f) tLastHalf = q.t;
			if (q.t > tPeak && tGone < 0 && tot < 0.1f) tGone = q.t;
		}
		float maxDuringWater = 0;
		for (auto & q : rows) if (q.t >= 10 && q.t < 40) maxDuringWater = std::max(maxDuringWater, q.v[0] + q.v[1] + q.v[2]);
		writeCsv("T4_moisture_memory", rows);
		img.save("T4_moisture_memory");
		Sample d = sample(m, 300, dry);
		printf("T4 moisture memory (water on a dry strip 10-40 s): strip M %.2f at stop, halves %.0f s after stop\n", mAtStop, tHalf);
		printf("   strip cover (water stopped at 40 s): max %.2f while under water; > 0.1 from %.0f s; peak %.2f at %.0f s; last >= 0.5 at %.0f s; < 0.1 (bare) from %.0f s; dry control cover %.3f\n",
			maxDuringWater, tGrow, peak, tPeak, tLastHalf, tGone, d.v[0] + d.v[1] + d.v[2]);
	}

	if (want("T5")) {
		SM probe;
		bool okD = probe.setParams(designer, err);
		printf("T5 presets: designer %s\n", okD ? "accepted" : ("REFUSED: " + err).c_str());
		bool okP = probe.setParams(published, err);
		printf("   published (TODO zeros) %s\n", okP ? "ACCEPTED (FAIL)" : ("refused: " + err).c_str());
		bool okU = probe.setParams(unknownUnit, err);
		printf("   unknown_unit.xml %s\n", okU ? "ACCEPTED (FAIL)" : ("refused: " + err).c_str());
		// Previous preset stays active: probe (designer, then two refusals)
		// must behave exactly like a fresh designer model.
		SM fresh; fresh.setParams(designer, err);
		World w1(GW, GH, FLAT_MM), w2(GW, GH, FLAT_MM);
		for (int s = 0; s < 1600; s++) { w1.frame(probe); w2.frame(fresh); }
		bool same = true;
		for (int k = 0; k < 3; k++) same = same && probe.getMeanCover(k) == fresh.getMeanCover(k);
		printf("   after the refusals the designer ranges are still active (identical to a fresh designer run over 40 s): %s\n", same ? "PASS" : "FAIL");
		bool okA = probe.setParams(alt, err);
		SM m; m.setParams(alt, err);
		World w(GW, GH, FLAT_MM);
		SeqResult r = runSequence(m, w, 360, "T5_alt_soil_preset", shots);
		Sample f = r.rows.back(), g = t1.rows.empty() ? f : t1.rows.back();
		printf("   alt_soil.xml %s; at 360 s: pioneer %.2f intermediate %.2f climax %.2f (designer: %.2f %.2f %.2f) -> dominant %s vs %s\n",
			okA ? "accepted" : "refused", f.v[0], f.v[1], f.v[2], g.v[0], g.v[1], g.v[2], ROLE[dominant(f)], ROLE[dominant(g)]);
	}

	if (want("T6")) {
		SM m; m.setParams(designer, err);
		World w(519, 295, FLAT_MM);
		w.noiseMM = 2.0f;
		for (int s = 0; s < (int)(600 / DT); s++) w.frame(m);
		printf("T6 noise (519x295, +/-2 mm uniform per frame, 600 s): disturbance events %lld -> %s\n",
			m.getDisturbanceEvents(), m.getDisturbanceEvents() == 0 ? "PASS" : "FAIL");
	}

	if (want("T7")) {
		SM m; m.setParams(designer, err);
		World w(519, 295, FLAT_MM);
		w.noiseMM = 2.0f;
		std::vector<float> ms;
		for (int s = 0; s < 2400; s++) { w.frame(m); if (s >= 100) ms.push_back(m.getLastStepMs()); }
		double sum = 0; for (float x : ms) sum += x;
		std::sort(ms.begin(), ms.end());
		printf("T7 step cost (519x295, UPDATE_INTERLEAVE %d), 2300 steps: mean %.2f ms, median %.2f, 90th pct %.2f, worst %.2f -> %s\n",
			SM::UPDATE_INTERLEAVE, sum / ms.size(), ms[ms.size() / 2], ms[ms.size() * 9 / 10], ms.back(), ms[ms.size() / 2] < 3.0f ? "PASS (median)" : "FAIL");
		int saved = SM::UPDATE_INTERLEAVE; SM::UPDATE_INTERLEAVE = 1;
		SM f; f.setParams(designer, err); ms.clear();
		for (int s = 0; s < 600; s++) { w.frame(f); if (s >= 50) ms.push_back(f.getLastStepMs()); }
		std::sort(ms.begin(), ms.end());
		printf("   for reference, full-grid step (UPDATE_INTERLEAVE 1): median %.2f ms\n", ms[ms.size() / 2]);
		SM::UPDATE_INTERLEAVE = saved;
	}
	return 0;
}
