// Headless check of the grain dither and seed particles (2026-11-10).
// Runs the real SuccessionModel.cpp and SeedParticles.cpp on
//   T1: flat moist ground from bare (S = 0), 360 s;
//   M:  a mound with a water channel, a patch dug on its flank at 200 s, 400 s;
// and renders both layers on unlit sand (band fill off) the way the
// shader and SeedLayer do: Bayer 4x4 dither in projector pixels (pioneer
// 1 px, intermediate 2 px, climax 4 px grain), role colour mixed from grey
// by soil; seeds as squares alpha-blended into a black FBO that fades each
// frame and is added over the sand. Writes PNG frames (every 2 s), a
// contact sheet per run, and prints the checks (row striping of the spawn
// signal vs a naive frame difference, seeds at the regrowth front, update
// time at 20,000 particles).
//
// Usage: ./grain_seed_harness <outdir> [repoRoot] [M]   (M: mound run only)
// Frames: <run>_NNNN.png (band fill off), <run>fill_NNNN.png (band fill on).
#include "../../src/Games/SuccessionModel.h"
#include "../../src/Games/SeedParticles.h"
#include "../dewitz_harness/png.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

typedef SuccessionModel SM;
static std::string OUT, ROOT = "../..";
static const float DT = 1.0f / 40.0f;
static const float MM_PER_CELL = 1.9f;
static const float TABLE_MM = -15.0f;
static const int GW = 160, GH = 100;
static const int PX = 2;                 // projector pixels per cell (~1.05 px/mm)
static const int FW = GW * PX, FH = GH * PX;
// Display settings (defaults of PlantLayer / SeedLayer).
static const float GRAIN_SCALE = 1.0f;
static const float GRAIN_BRIGHTNESS_DARK = 1.5f; // band fill off
static const float GRAIN_BRIGHTNESS_FILL = 1.0f; // band fill on
static const float SEED_SIZE_PX = 4.0f;          // soft point diameter
static const float SEED_FADE = 0.125f;           // FBO fade per frame
static const float SEED_BRIGHTNESS = 1.0f;
static const float SEED_OPACITY_ON_FILL = 0.7f;
static const float SOIL_TINT = 0.35f;            // VegetationField::SOIL_TINT_STRENGTH

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

// ------------------------------------------------------------ colour map (band fill on)
struct Key { float h; float r, g, b; };
static std::vector<Key> colourMap;
static void readColourMap(const std::string & path)
{
	std::ifstream f(path);
	std::stringstream ss; ss << f.rdbuf();
	std::string doc = ss.str();
	size_t pos = 0;
	while ((pos = doc.find("<key>", pos)) != std::string::npos) {
		size_t end = doc.find("</key>", pos);
		std::string k = doc.substr(pos, end - pos);
		colourMap.push_back({ (float)atof(tag(k, "height").c_str()), (float)atof(tag(k, "color-r").c_str()) / 255.0f,
			(float)atof(tag(k, "color-g").c_str()) / 255.0f, (float)atof(tag(k, "color-b").c_str()) / 255.0f });
		pos = end;
	}
}
static const Key & colourAt(float h) // stepped: the last key at or below
{
	const Key * k = &colourMap.front();
	for (const Key & c : colourMap) if (c.h <= h) k = &c;
	return *k;
}

// ------------------------------------------------------------ shader copy
// Bayer 4x4 threshold, as heightMapShader.frag's bayer4(): index =
// 4 b2(x mod 2, y mod 2) + b2(floor(x/2) mod 2, floor(y/2) mod 2),
// b2(x, y) = 2x + 3y - 4xy; threshold (index + 0.5) / 16.
static float bayer4(int x, int y)
{
	auto b2 = [](int a, int b) { return 2 * a + 3 * b - 4 * a * b; };
	x &= 3; y &= 3;
	int idx = 4 * b2(x & 1, y & 1) + b2((x >> 1) & 1, (y >> 1) & 1);
	return (idx + 0.5f) / 16.0f;
}
static float tex8(float v) { return (float)(unsigned char)(std::min(1.0f, std::max(0.0f, v)) * 255.0f) / 255.0f; }

// ------------------------------------------------------------ world
struct World {
	std::vector<float> bed, noisy, above;
	std::vector<unsigned char> water;
	std::mt19937 rng{ 7 };
	SM m;
	SeedParticles seeds;
	std::vector<float> fbo;     // seed layer, premultiplied RGBA float, FW x FH
	World() : bed(GW * GH, 0.0f), noisy(GW * GH), above(GW * GH), water(GW * GH, 0), fbo((size_t)FW * FH * 4, 0.0f) {}
	void step() {
		std::uniform_real_distribution<float> U(-1.0f, 1.0f);
		for (int i = 0; i < GW * GH; i++) { noisy[i] = bed[i] + U(rng); above[i] = bed[i] - TABLE_MM; }
		SM::Inputs in;
		in.elevationMM = noisy.data(); in.water = water.data(); in.heightAboveTableMM = above.data();
		in.cols = GW; in.rows = GH; in.mmPerCell = MM_PER_CELL; in.dt = DT;
		in.temperature = 0.745f; in.restingTemperature = 0.745f; in.spreadRadiusMM = 40.0f;
		m.step(in);
		const float * covers[3] = { m.cover(0), m.cover(1), m.cover(2) };
		seeds.update(covers, m.soil(), water.data(), GW, GH, MM_PER_CELL, DT, SM::UPDATE_INTERLEAVE);
		drawSeedsIntoFbo();
	}
	// SeedLayer: fade all four channels, then a soft round point per
	// particle (coverage falls linearly to 0 at the rim), premultiplied "over".
	void drawSeedsIntoFbo() {
		for (float & v : fbo) v *= (1.0f - SEED_FADE);
		for (size_t i = 0; i < seeds.count(); i++) {
			float a = seeds.alpha(i);
			float r = 0.5f * SEED_SIZE_PX * seeds.sizeFactor(i);
			if (a <= 0.0f || r <= 0.0f) continue;
			float cx = seeds.posX(i) * PX, cy = seeds.posY(i) * PX;
			const float * c = seeds.rgb(i);
			for (int y = std::max(0, (int)std::floor(cy - r)); y <= std::min(FH - 1, (int)std::floor(cy + r)); y++)
				for (int x = std::max(0, (int)std::floor(cx - r)); x <= std::min(FW - 1, (int)std::floor(cx + r)); x++) {
					float d = std::hypot(x + 0.5f - cx, y + 0.5f - cy);
					if (d >= r) continue;
					float cov = a * (1.0f - d / r);
					float * p = &fbo[((size_t)y * FW + x) * 4];
					for (int k = 0; k < 3; k++) p[k] = std::min(1.0f, c[k] * SEED_BRIGHTNESS) * cov + p[k] * (1 - cov);
					p[3] = cov + p[3] * (1 - cov);
				}
		}
	}
	// Grain dither (as the shader) + water + seed FBO, on unlit sand.
	std::vector<unsigned char> render(bool fill = false, bool withSeeds = true, bool withGrain = true) const {
		std::vector<unsigned char> img((size_t)FW * FH * 3);
		const float base[3] = { 1.0f, 2.0f, 4.0f };
		for (int py = 0; py < FH; py++)
			for (int px = 0; px < FW; px++) {
				int i = (py / PX) * GW + px / PX;
				float rgb[3] = { 0, 0, 0 };
				if (fill && !water[i]) { // colour map at the bed + soil tint (land lines not drawn here)
					const Key & k = colourAt(bed[i]);
					float st = SOIL_TINT * tex8(m.soil()[i]);
					const float brown[3] = { 0.42f, 0.27f, 0.13f }, kc[3] = { k.r, k.g, k.b };
					for (int q = 0; q < 3; q++) rgb[q] = kc[q] + (brown[q] - kc[q]) * st;
				}
				if (water[i]) { // lakebed + water colour (t = 0.4), as the shader
					const float lake[3] = { 0.722f, 0.690f, 0.627f }, sh[3] = { 0.255f, 0.714f, 0.769f }, dp[3] = { 0.031f, 0.188f, 0.420f };
					for (int k = 0; k < 3; k++) { float w = sh[k] + (dp[k] - sh[k]) * 0.4f; rgb[k] = lake[k] + (w - lake[k]) * 0.74f; }
				} else if (withGrain) {
					// combinedTex: R = climax, G = pioneer, B = intermediate (8 bit); site R = soil
					float cov[3] = { tex8(m.cover(0)[i]), tex8(m.cover(1)[i]), tex8(m.cover(2)[i]) };
					float soil = tex8(m.soil()[i]);
					for (int r = 0; r < 3; r++) { // pioneer, intermediate, climax: coarser on top
						float size = base[r] * GRAIN_SCALE;
						int bx = (int)std::floor((px + 0.5f) / size), by = (int)std::floor((py + 0.5f) / size);
						if (bayer4(bx, by) < cov[r]) {
							float c[3];
							SeedParticles::roleColour(r, soil, c);
							float gb = fill ? GRAIN_BRIGHTNESS_FILL : GRAIN_BRIGHTNESS_DARK;
							for (int k = 0; k < 3; k++) rgb[k] = std::min(1.0f, c[k] * gb);
						}
					}
				}
				const float * f = &fbo[((size_t)py * FW + px) * 4];
				for (int k = 0; k < 3; k++) {
					float v = rgb[k];
					if (withSeeds) // fill off: additive; fill on: premultiplied "over" at the opacity
						v = fill ? v * (1.0f - SEED_OPACITY_ON_FILL * f[3]) + SEED_OPACITY_ON_FILL * f[k] : v + f[k];
					img[((size_t)py * FW + px) * 3 + k] = (unsigned char)std::lround(std::min(1.0f, v) * 255.0f);
				}
			}
		return img;
	}
};

// 3x5 digit font for the time labels.
static const char * DIGITS[11] = { "111101101101111", "010110010010111", "111001111100111", "111001111001111",
	"101101111001001", "111100111001111", "111100111101111", "111001001001001", "111101111101111", "111101111001111",
	"000011110011110" };
static void label(std::vector<unsigned char> & img, int w, int x, int y, const std::string & text)
{
	const int S = 2;
	for (size_t c = 0; c < text.size(); c++) {
		int d = text[c] == 's' ? 10 : text[c] - '0';
		for (int py = -1; py < 5 * S + 1; py++) for (int px = -1; px < 3 * S + 1; px++) {
			unsigned char * p = &img[((size_t)(y + py) * w + x + (int)c * 4 * S + px) * 3];
			p[0] = p[1] = p[2] = 255;
		}
		for (int r = 0; r < 5; r++) for (int col = 0; col < 3; col++) if (DIGITS[d][r * 3 + col] == '1')
			for (int sy = 0; sy < S; sy++) for (int sx = 0; sx < S; sx++) {
				unsigned char * p = &img[((size_t)(y + r * S + sy) * w + x + (int)c * 4 * S + col * S + sx) * 3];
				p[0] = p[1] = p[2] = 20;
			}
	}
}

static void sheet(const std::string & name, const std::vector<std::vector<unsigned char>> & frames,
	const std::vector<int> & times, int columns)
{
	const int gap = 4, n = (int)frames.size(), rows = (n + columns - 1) / columns;
	const int W = columns * FW + (columns + 1) * gap, H = rows * FH + (rows + 1) * gap;
	std::vector<unsigned char> s((size_t)W * H * 3, 255);
	for (int k = 0; k < n; k++) {
		int ox = gap + (k % columns) * (FW + gap), oy = gap + (k / columns) * (FH + gap);
		for (int y = 0; y < FH; y++)
			std::copy(&frames[k][(size_t)y * FW * 3], &frames[k][(size_t)(y + 1) * FW * 3], &s[((size_t)(oy + y) * W + ox) * 3]);
		label(s, W, ox + 3, oy + 3, std::to_string(times[k]) + "s");
	}
	png::writeRGB(OUT + "/" + name + ".png", W, H, s);
}

// Spawn rows of the particles born this step (age == dt).
static void countSpawnRows(const SeedParticles & s, std::vector<double> & perRowClass, int & n)
{
	for (size_t i = 0; i < s.count(); i++)
		if (s.ageOf(i) <= DT * 1.001f && s.kindOf(i) == SeedParticles::KIND_SEED) { // born this step
			perRowClass[((int)s.posY(i)) % 3] += 1.0;
			n++;
		}
}

static void run(const std::string & name, World & w, int seconds, int digAt,
	int dx0, int dy0, int dx1, int dy1)
{
	std::vector<std::vector<unsigned char>> frames;
	std::vector<int> times;
	std::vector<double> rowClass(3, 0.0);
	int spawnsCounted = 0;
	double frontSpawns = 0, frontTotal = 0;
	std::vector<std::vector<unsigned char>> framesFill;
	double countSum = 0; size_t countMax = 0; int countN = 0;
	std::vector<float> prevCover;
	const int steps = (int)(seconds / DT);
	for (int s = 1; s <= steps; s++) {
		float t = s * DT;
		if (digAt > 0 && s == (int)(digAt / DT)) // dig a patch 8 mm deep
			for (int y = dy0; y < dy1; y++) for (int x = dx0; x < dx1; x++) w.bed[y * GW + x] -= 8.0f;
		// naive per-step difference, for comparison (one sample at 60 s)
		if (s == (int)(60 / DT)) prevCover.assign(w.m.cover(0), w.m.cover(0) + GW * GH);
		w.step();
		if (s == (int)(60 / DT) + 0 && !prevCover.empty()) {
			double cls[3] = { 0, 0, 0 };
			for (int y = 0; y < GH; y++) for (int x = 0; x < GW; x++) {
				float d = w.m.cover(0)[y * GW + x] - prevCover[y * GW + x];
				if (d > 0) cls[y % 3] += d;
			}
			double tot = cls[0] + cls[1] + cls[2];
			if (tot > 0)
				printf("  %s naive one-step difference at 60 s, pioneer growth by row mod 3: %.0f%% / %.0f%% / %.0f%%\n",
					name.c_str(), 100 * cls[0] / tot, 100 * cls[1] / tot, 100 * cls[2] / tot);
		}
		countSpawnRows(w.seeds, rowClass, spawnsCounted);
		if (digAt > 0 && t > digAt + 5 && t < digAt + 60) {
			// seeds born near the dug patch's edge (within 4 cells of it) vs all
			for (size_t i = 0; i < w.seeds.count(); i++)
				if (w.seeds.ageOf(i) <= DT * 1.001f && w.seeds.kindOf(i) == SeedParticles::KIND_SEED) {
					int x = (int)w.seeds.posX(i), y = (int)w.seeds.posY(i);
					bool nearPatch = x >= dx0 - 4 && x < dx1 + 4 && y >= dy0 - 4 && y < dy1 + 4;
					frontTotal += 1; if (nearPatch) frontSpawns += 1;
				}
		}
		countSum += w.seeds.count(); countMax = std::max(countMax, w.seeds.count()); countN++;
		if (s % (int)(2 / DT) == 0) {
			std::vector<unsigned char> img = w.render(false), imgFill = w.render(true);
			char fn[256];
			snprintf(fn, sizeof fn, "%s/%s_%04d.png", OUT.c_str(), name.c_str(), (int)std::lround(t));
			png::writeRGB(fn, FW, FH, img);
			snprintf(fn, sizeof fn, "%s/%sfill_%04d.png", OUT.c_str(), name.c_str(), (int)std::lround(t));
			png::writeRGB(fn, FW, FH, imgFill);
			if (((int)std::lround(t)) % 30 == 0 || (digAt > 0 && (int)std::lround(t) == digAt + 10)) {
				frames.push_back(img); framesFill.push_back(imgFill); times.push_back((int)std::lround(t));
			}
		}
	}
	sheet(name + "_contact_sheet", frames, times, 4);
	sheet(name + "fill_contact_sheet", framesFill, times, 4);
	printf("  %s seeds alive: mean %.0f, max %zu (cap %d); %d seeds spawned (%.1f per s)\n", name.c_str(),
		countSum / std::max(1, countN), countMax, SeedParticles::MAX_PARTICLES, spawnsCounted, spawnsCounted / (float)seconds);
	double tot = rowClass[0] + rowClass[1] + rowClass[2];
	printf("  %s seed spawns by row mod 3 over the run: %.1f%% / %.1f%% / %.1f%% (%d spawns)\n", name.c_str(),
		100 * rowClass[0] / std::max(1.0, tot), 100 * rowClass[1] / std::max(1.0, tot), 100 * rowClass[2] / std::max(1.0, tot), spawnsCounted);
	if (frontTotal > 0) {
		double patchArea = (double)(dx1 - dx0 + 8) * (dy1 - dy0 + 8) / (GW * GH);
		printf("  %s seeds born %d-%d s after the dig within 4 cells of the patch: %.0f%% (that area is %.0f%% of the box)\n",
			name.c_str(), digAt + 5, digAt + 60, 100 * frontSpawns / frontTotal, 100 * patchArea);
	}
}

static void benchmark(bool pioneerOnly)
{
	// Real grid size, current defaults; every cell grows at its own random
	// rate (worst case for the window close and the percentile).
	const int C = 519, R = 295, N = C * R;
	std::vector<float> cov[3], rate(N);
	for (auto & v : cov) v.assign(N, 0.0f);
	std::mt19937 g(3);
	std::uniform_real_distribution<float> U(0.0001f, 0.002f);
	for (float & r : rate) r = U(g);
	std::vector<float> soil(N, 0.5f);
	std::vector<unsigned char> water(N, 0);
	SeedParticles s;
	bool saved = SeedParticles::PIONEER_ONLY;
	SeedParticles::PIONEER_ONLY = pioneerOnly;
	std::vector<double> all;
	double first = 0.0; size_t maxCount = 0;
	for (int f = 0; f < 1200; f++) {
		for (int r = 0; r < 3; r++) for (int i = 0; i < N; i++) cov[r][i] = std::min(1.0f, cov[r][i] + rate[i]);
		if (f % 400 == 399) for (int r = 0; r < 3; r++) std::fill(cov[r].begin(), cov[r].end(), 0.0f);
		const float * cv[3] = { cov[0].data(), cov[1].data(), cov[2].data() };
		s.update(cv, soil.data(), water.data(), C, R, 1.9f, 1.0f / 60.0f, 3);
		maxCount = std::max(maxCount, s.count());
		if (f < 60) first = std::max(first, s.lastUpdateMs()); else all.push_back(s.lastUpdateMs());
	}
	SeedParticles::PIONEER_ONLY = saved;
	std::sort(all.begin(), all.end());
	double sum = 0; for (double v : all) sum += v;
	printf("benchmark (519 x 295 grid, every cell growing, defaults, pioneer only %s): max particles %zu (cap %d); "
		"update mean %.2f ms, p99 %.2f ms, worst %.2f ms (after the first second; first second worst %.2f ms)\n",
		pioneerOnly ? "on" : "off", maxCount, SeedParticles::MAX_PARTICLES, sum / all.size(),
		all[(size_t)(0.99 * (all.size() - 1))], all.back(), first);
}

int main(int argc, char ** argv)
{
	if (argc < 2) { fprintf(stderr, "usage: %s <outdir> [repoRoot]\n", argv[0]); return 1; }
	OUT = argv[1];
	if (argc > 2) ROOT = argv[2];
	SM::RoleParams designer[3];
	if (!readPreset(ROOT + "/bin/data/settings/ecologyParamsDesigner.xml", designer)) { fprintf(stderr, "no preset\n"); return 1; }
	std::string err;

	readColourMap(ROOT + "/bin/data/colorMaps/HeightColorMap.xml");
	const bool onlyM = argc > 3 && std::string(argv[3]) == "M";
	benchmark(true);
	benchmark(false);

	if (!onlyM) { // T1: flat moist ground from bare (S = 0)
		World w; w.m.setParams(designer, err);
		printf("T1 (flat, moist, S = 0), 360 s:\n");
		run("T1", w, 360, -1, 0, 0, 0, 0);
	}
	{ // M: mound with a water channel; dig a patch on its flank at 200 s
		World w; w.m.setParams(designer, err);
		const float cx = 70, cy = 45, peak = 45.0f, tan30 = 0.57735f;
		for (int y = 0; y < GH; y++) for (int x = 0; x < GW; x++) {
			float r = std::hypot(x - cx, y - cy) * MM_PER_CELL;
			float h = std::max(0.0f, peak - r * tan30);
			if (y >= 84 && y < 92) { h = -20.0f; w.water[y * GW + x] = 1; } // channel with water
			w.bed[y * GW + x] = h;
		}
		printf("M (mound + water channel, patch dug at 300 s), 420 s:\n");
		run("M", w, 420, 300, 96, 30, 116, 50);
	}
	return 0;
}
