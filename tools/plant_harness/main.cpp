// Headless check of the plant display (PlantLayer, 2026-11-03): runs the
// succession harness's T1 (primary sequence on flat moist ground) and T3
// (two patches disturbed after T1) with the real SuccessionModel.cpp and
// PlantLayer.cpp, renders a frame every 10 s the way the shader does
// (stepped colour map from HeightColorMap.xml, soil tint, plants via
// PlantLayer::renderOver), and writes a contact sheet per run.
//
// Usage: ./plant_harness <outdir> [repoRoot]
#include "../../src/Games/SuccessionModel.h"
#include "../../src/Games/PlantLayer.h"
#include "../dewitz_harness/png.h"
#include <algorithm>
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
static const int GW = 160, GH = 100;   // as the succession harness (304 x 190 mm)
static const float PX_PER_MM = 1.5f;   // contact-sheet frame scale
static const float SOIL_TINT = 0.35f;  // VegetationField::SOIL_TINT_STRENGTH default

// ------------------------------------------------------------ files
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

struct Key { float h; int r, g, b; };
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
		colourMap.push_back({ (float)atof(tag(k, "height").c_str()), atoi(tag(k, "color-r").c_str()),
			atoi(tag(k, "color-g").c_str()), atoi(tag(k, "color-b").c_str()) });
		pos = end;
	}
}
// The map is stepped (pairs of equal-colour keys), so the colour of the
// last key at or below the height is what the shader shows.
static const Key & colourAt(float h)
{
	const Key * k = &colourMap.front();
	for (const Key & c : colourMap) if (c.h <= h) k = &c;
	return *k;
}

// ------------------------------------------------------------ world
struct World {
	std::vector<float> bed, noisy, above;
	std::vector<unsigned char> water;
	std::mt19937 rng{ 7 };
	World() : bed(GW * GH, 0.0f), noisy(GW * GH), above(GW * GH), water(GW * GH, 0) {}
	void frame(SM & m, PlantLayer & plants) {
		std::uniform_real_distribution<float> U(-1.0f, 1.0f);
		for (int i = 0; i < GW * GH; i++) { noisy[i] = bed[i] + U(rng); above[i] = bed[i] - TABLE_MM; }
		SM::Inputs in;
		in.elevationMM = noisy.data(); in.water = water.data(); in.heightAboveTableMM = above.data();
		in.cols = GW; in.rows = GH; in.mmPerCell = MM_PER_CELL; in.dt = DT;
		in.temperature = 0.745f; in.restingTemperature = 0.745f; in.spreadRadiusMM = 40.0f;
		m.step(in);
		const float * covers[3] = { m.cover(0), m.cover(1), m.cover(2) };
		plants.update(covers, water.data(), GW, GH, MM_PER_CELL, DT);
	}
};

// ------------------------------------------------------------ rendering
static const int FW = (int)(GW * MM_PER_CELL * PX_PER_MM), FH = (int)(GH * MM_PER_CELL * PX_PER_MM);

// 3x5 digit font for the time labels.
static const char * DIGITS[11] = { "111101101101111", "010110010010111", "111001111100111", "111001111001111",
	"101101111001001", "111100111001111", "111100111101111", "111001001001001", "111101111101111", "111101111001111",
	"000011110011110" /* s */ };
static void label(std::vector<unsigned char> & img, int w, int x, int y, const std::string & text)
{
	const int S = 3;
	for (size_t c = 0; c < text.size(); c++) {
		int d = text[c] == 's' ? 10 : text[c] - '0';
		for (int py = -1; py < 5 * S + 1; py++) for (int px = -1; px < 3 * S + 1; px++) { // white backing
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

static std::vector<unsigned char> renderFrame(const World & w, const SM & m, const PlantLayer & plants,
	const std::vector<std::pair<int, int>> & outline)
{
	std::vector<unsigned char> img((size_t)FW * FH * 3);
	for (int y = 0; y < FH; y++)
		for (int x = 0; x < FW; x++) {
			int gx = std::min(GW - 1, (int)(x / PX_PER_MM / MM_PER_CELL)), gy = std::min(GH - 1, (int)(y / PX_PER_MM / MM_PER_CELL));
			int i = gy * GW + gx;
			const Key & k = colourAt(w.bed[i]);
			float s = m.soil()[i] * SOIL_TINT; // soil tint, as the shader (not on water)
			unsigned char * p = &img[((size_t)y * FW + x) * 3];
			p[0] = (unsigned char)(k.r * (1 - s) + 0.42f * 255 * s);
			p[1] = (unsigned char)(k.g * (1 - s) + 0.27f * 255 * s);
			p[2] = (unsigned char)(k.b * (1 - s) + 0.13f * 255 * s);
		}
	plants.renderOver(img, FW, FH, PX_PER_MM);
	for (auto & o : outline) { // patch outlines (test annotation only)
		unsigned char * p = &img[((size_t)o.second * FW + o.first) * 3];
		p[0] = 200; p[1] = 30; p[2] = 30;
	}
	return img;
}

static void contactSheet(const std::string & name, const std::vector<std::vector<unsigned char>> & frames,
	const std::vector<int> & times, int columns)
{
	const int gap = 6, n = (int)frames.size(), rows = (n + columns - 1) / columns;
	const int W = columns * FW + (columns + 1) * gap, H = rows * FH + (rows + 1) * gap;
	std::vector<unsigned char> sheet((size_t)W * H * 3, 255);
	for (int k = 0; k < n; k++) {
		int ox = gap + (k % columns) * (FW + gap), oy = gap + (k / columns) * (FH + gap);
		for (int y = 0; y < FH; y++)
			std::copy(&frames[k][(size_t)y * FW * 3], &frames[k][(size_t)(y + 1) * FW * 3], &sheet[((size_t)(oy + y) * W + ox) * 3]);
		label(sheet, W, ox + 4, oy + 4, std::to_string(times[k]) + "s");
	}
	png::writeRGB(OUT + "/" + name + ".png", W, H, sheet);
}

static std::vector<std::pair<int, int>> rectOutline(int x0, int y0, int x1, int y1)
{
	std::vector<std::pair<int, int>> o;
	int X0 = (int)(x0 * MM_PER_CELL * PX_PER_MM), X1 = (int)(x1 * MM_PER_CELL * PX_PER_MM) - 1;
	int Y0 = (int)(y0 * MM_PER_CELL * PX_PER_MM), Y1 = (int)(y1 * MM_PER_CELL * PX_PER_MM) - 1;
	for (int x = X0; x <= X1; x++) { o.push_back({ x, Y0 }); o.push_back({ x, Y1 }); }
	for (int y = Y0; y <= Y1; y++) { o.push_back({ X0, y }); o.push_back({ X1, y }); }
	return o;
}

int main(int argc, char ** argv)
{
	if (argc < 2) { fprintf(stderr, "usage: %s <outdir> [repoRoot]\n", argv[0]); return 1; }
	OUT = argv[1];
	if (argc > 2) ROOT = argv[2];
	SM::RoleParams designer[3];
	if (!readPreset(ROOT + "/bin/data/settings/ecologyParamsDesigner.xml", designer)) { fprintf(stderr, "no preset\n"); return 1; }
	readColourMap(ROOT + "/bin/data/colorMaps/HeightColorMap.xml");
	std::string err;
	SM m; m.setParams(designer, err);
	PlantLayer plants;
	World w;

	// T1: 0..360 s, a frame every 10 s.
	std::vector<std::vector<unsigned char>> frames;
	std::vector<int> times;
	const int every = (int)(10.0f / DT);
	w.frame(m, plants); // the model's fields exist after its first step
	frames.push_back(renderFrame(w, m, plants, {})); times.push_back(0);
	for (int s = 2; s <= (int)(360 / DT); s++) {
		w.frame(m, plants);
		if (s % every == 0) { frames.push_back(renderFrame(w, m, plants, {})); times.push_back(s / 40); }
	}
	contactSheet("T1_plants_contact_sheet", frames, times, 6);
	printf("T1: %zu frames (0-360 s), %d x %d plant sites at %.0f mm\n", frames.size(), plants.sitesX(), plants.sitesY(), plants.builtSpacingMM());

	// T3: continue; patches A and B raised 10 mm at t = 360 s, B set to S = 0
	// (new fill), 420 s more.
	const int ax0 = 20, ay0 = 30, ax1 = 60, ay1 = 70, bx0 = 100, by0 = 30, bx1 = 140, by1 = 70;
	for (int y = ay0; y < ay1; y++) for (int x = ax0; x < ax1; x++) w.bed[y * GW + x] += 10.0f;
	for (int y = by0; y < by1; y++) for (int x = bx0; x < bx1; x++) w.bed[y * GW + x] += 10.0f;
	std::vector<std::pair<int, int>> outline = rectOutline(ax0, ay0, ax1, ay1), ob = rectOutline(bx0, by0, bx1, by1);
	outline.insert(outline.end(), ob.begin(), ob.end());
	frames.clear(); times.clear();
	frames.push_back(renderFrame(w, m, plants, outline)); times.push_back(360);
	for (int s = 1; s <= (int)(420 / DT); s++) {
		w.frame(m, plants);
		if (s == 2)
			for (int y = by0; y < by1; y++) for (int x = bx0; x < bx1; x++) m.soilMutable()[y * GW + x] = 0.0f;
		if (s % every == 0) { frames.push_back(renderFrame(w, m, plants, outline)); times.push_back(360 + s / 40); }
	}
	contactSheet("T3_plants_contact_sheet", frames, times, 6);
	printf("T3: %zu frames (360-780 s); patch A (left, old soil) and B (right, new fill) outlined in red\n", frames.size());
	return 0;
}
