// Headless harness for src/Games/DewitzFlow.cpp (no openFrameworks needed:
// DewitzFlow is standard-library only, so this compiles the real file).
// Builds synthetic heightmaps, runs the flow (with fresh Kinect-like noise
// every frame on the box-sized terrains), advects particles, and writes a
// particle-density PNG and a lattice-speed PNG per run, plus readouts.
//
// Usage: dewitz_harness <outdir> [frames] [proto_terrain.f32]
//   proto_terrain.f32 comes from export_proto_terrain.py: the exact
//   heightmap of tools/dewitz_proto.py, run with that prototype's settings.
#include "../../src/Games/DewitzFlow.h"
#include "png.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <random>
#include <string>
#include <vector>

static const float DT = 1.0f / 40.0f; // ~40 fps measured on hardware

struct Terrain {
	std::string name;
	int cols, rows;
	float mmPerCell;
	float noiseMM;              // fresh per-frame noise (0 = static terrain)
	std::vector<float> base;
	// Regions for the acceptance checks (fine-grid coordinates).
	float pitX = -1, pitY = -1, pitR = 0;
	float gapX = -1, gapY0 = 0, gapY1 = 0;
	int scale = 1;              // PNG upscale
};

static float gauss2(float x, float y, float cx, float cy, float s) { float dx = x - cx, dy = y - cy; return std::exp(-(dx * dx + dy * dy) / (2 * s * s)); }

// Box-sized terrains: this box's Kinect ROI grid (519x295, ~1.9 mm/cell).
static Terrain boxTerrain(const std::string & name)
{
	Terrain t{ name, 519, 295, 1.9f, 1.5f, std::vector<float>(519 * 295, 0.0f) };
	for (int y = 0; y < t.rows; y++) for (int x = 0; x < t.cols; x++) {
		float v = 0.0f;
		if (name == "mounds_ridge") {
			v += 60.0f * gauss2(x, y, 150, 85, 30) + 60.0f * gauss2(x, y, 150, 210, 30);
			float ridge = 50.0f * std::exp(-(x - 340.0f) * (x - 340.0f) / (2 * 8.0f * 8.0f));
			float gap = 1.0f - std::exp(-std::pow((y - 147.0f) / 22.0f, 6.0f));
			v += ridge * gap;
		} else if (name == "pit") {
			v -= 40.0f * gauss2(x, y, 260, 147, 30);
		} else if (name == "proto_layout") {
			// tools/dewitz_proto.py's layout (240x150) scaled to this grid:
			// mounds off the approach path, ridge with a gap, a pit.
			const float sx = 519.0f / 240.0f, sy = 295.0f / 150.0f;
			float X = x / sx, Y = y / sy;
			v += 40.0f * gauss2(X, Y, 70, 45, 14) + 45.0f * gauss2(X, Y, 150, 105, 18);
			v += 35.0f * std::exp(-(X - 115) * (X - 115) / (2 * 5.0f * 5.0f)) * ((Y < 60 || Y > 85) ? 1.0f : 0.0f);
			v -= 25.0f * gauss2(X, Y, 195, 50, 16);
		}
		t.base[y * t.cols + x] = v;
	}
	if (name == "mounds_ridge") { t.gapX = 340; t.gapY0 = 129; t.gapY1 = 165; }
	if (name == "pit") { t.pitX = 260; t.pitY = 147; t.pitR = 25; }
	if (name == "proto_layout") { t.pitX = 195 * 519.0f / 240; t.pitY = 50 * 295.0f / 150; t.pitR = 20;
		t.gapX = 115 * 519.0f / 240; t.gapY0 = 62 * 295.0f / 150; t.gapY1 = 83 * 295.0f / 150; }
	return t;
}

static void boxDefaults()
{
	DewitzFlow::DOWNSAMPLE = 3; DewitzFlow::TAU = 1.98f; DewitzFlow::USE_MRT = true; DewitzFlow::INLET_SPEED = 0.08f;
	DewitzFlow::INLET_EDGE = DewitzFlow::EDGE_LEFT; DewitzFlow::STEPS_PER_FRAME = 1;
	DewitzFlow::OBSTACLE_HEIGHT_MM = 25.0f; DewitzFlow::OBSTACLE_SLOPE = 0.8f; DewitzFlow::GRADIENT_SATURATION = 0.5f;
	DewitzFlow::W_FLOW = 1.0f; DewitzFlow::W_GRAD = 0.5f; DewitzFlow::W_ATTR = 0.5f; DewitzFlow::SPEED = 100.0f;
	DewitzFlow::PARTICLE_COUNT = 20000; DewitzFlow::PARTICLE_LIFE_SEC = 15.0f;
	if (const char * e = getenv("DEWITZ_GRAD_SAT")) DewitzFlow::GRADIENT_SATURATION = (float)atof(e); // for sweeps
}

// tools/dewitz_proto.py's settings, translated: lattice = its grid
// (DOWNSAMPLE 1, 1 unit per cell), h > 18 or slope > 4 is solid, downhill
// saturates at slope 2 (its min(norm/2, 1)), speed 1.2 cells/frame, 40000
// particles living 600 frames, tau 1.98, inlet 0.08.
static void protoDefaults()
{
	boxDefaults();
	DewitzFlow::DOWNSAMPLE = 1; DewitzFlow::OBSTACLE_HEIGHT_MM = 18.0f; DewitzFlow::OBSTACLE_SLOPE = 4.0f;
	DewitzFlow::GRADIENT_SATURATION = 2.0f; DewitzFlow::SPEED = 1.2f / DT; DewitzFlow::PARTICLE_COUNT = 40000;
	DewitzFlow::PARTICLE_LIFE_SEC = 600 * DT;
}

static void run(const Terrain & t, const std::string & tag, const std::string & outdir, int frames,
	std::function<void()> configure)
{
	configure();
	std::vector<float> noisy(t.base.size());
	std::mt19937 rng(7); std::normal_distribution<float> N(0.0f, 1.0f);
	DewitzFlow flow;
	std::vector<float> img(t.cols * t.rows, 0.0f);       // FBO emulation: fade + additive points
	std::vector<float> occupancy(t.cols * t.rows, 0.0f); // time-averaged particle count
	double latSum = 0, partSum = 0, flipSum = 0, exitSum = 0, cohSum = 0; int cohN = 0, statN = 0;
	float worstLat = 0;
	std::vector<float> oldX, oldY;
	for (int fr = 0; fr < frames; fr++) {
		for (size_t i = 0; i < t.base.size(); i++) noisy[i] = t.base[i] + (t.noiseMM > 0 ? t.noiseMM * N(rng) : 0.0f);
		oldX = flow.particleX(); oldY = flow.particleY();
		flow.update(noisy.data(), t.cols, t.rows, t.mmPerCell, DT);
		const auto & X = flow.particleX(); const auto & Y = flow.particleY();
		if (fr >= frames - 1000) {
			latSum += flow.getLatticeMs(); partSum += flow.getParticleMs(); flipSum += flow.getMaskFlips();
			exitSum += flow.getExitsLastFrame(); statN++;
			worstLat = std::max(worstLat, flow.getLatticeMs());
			for (size_t k = 0; k < X.size() && k < oldX.size(); k += 7) {
				float dx = X[k] - oldX[k], dy = Y[k] - oldY[k], d = std::sqrt(dx * dx + dy * dy);
				if (d > 1e-4f && d < 0.5f * DewitzFlow::SPEED) { cohSum += dx / d; cohN++; } // skip respawn jumps
			}
		}
		if (fr >= frames - 400) {
			for (float & v : img) v *= 0.92f;
			for (size_t k = 0; k < X.size(); k++) {
				int x = (int)X[k], y = (int)Y[k];
				if (x >= 0 && y >= 0 && x < t.cols && y < t.rows) { img[y * t.cols + x] += 0.35f; occupancy[y * t.cols + x] += 1.0f; }
			}
		}
	}

	// Composite: terrain grey ramp, obstacles dark red, particles additive.
	int s = t.scale, W = t.cols * s, H = t.rows * s, ds = DewitzFlow::DOWNSAMPLE;
	std::vector<unsigned char> rgb((size_t)W * H * 3);
	for (int Yp = 0; Yp < H; Yp++) for (int Xp = 0; Xp < W; Xp++) {
		int x = Xp / s, y = Yp / s;
		float e = t.base[y * t.cols + x];
		float g = 40.0f + std::max(-40.0f, std::min(60.0f, e)) * 0.6f;
		float r = g, gg = g, b = g;
		int i = std::min(x / ds, flow.latticeWidth() - 1), j = std::min(y / ds, flow.latticeHeight() - 1);
		if (flow.isObstacle(i, j)) { r = 110; gg = 30; b = 30; }
		float p = std::min(1.0f, img[y * t.cols + x]);
		r += 160 * p; gg += 235 * p; b += 255 * p;
		unsigned char * o = &rgb[((size_t)Yp * W + Xp) * 3];
		o[0] = (unsigned char)std::min(255.0f, r); o[1] = (unsigned char)std::min(255.0f, gg); o[2] = (unsigned char)std::min(255.0f, b);
	}
	png::writeRGB(outdir + "/" + t.name + tag + ".png", W, H, rgb);

	// Lattice speed (obstacles red).
	int lw = flow.latticeWidth(), lh = flow.latticeHeight();
	std::vector<unsigned char> sp((size_t)lw * lh * 3);
	float ms = std::max(1e-6f, flow.getMaxSpeed());
	for (int j = 0; j < lh; j++) for (int i = 0; i < lw; i++) {
		unsigned char * o = &sp[((size_t)j * lw + i) * 3];
		if (flow.isObstacle(i, j)) { o[0] = 120; o[1] = 20; o[2] = 20; continue; }
		float v = std::min(1.0f, flow.speedAt(i, j) / ms);
		o[0] = (unsigned char)(255 * v); o[1] = (unsigned char)(255 * v); o[2] = (unsigned char)(80 + 175 * v);
	}
	png::writeRGB(outdir + "/" + t.name + tag + "_speed.png", lw, lh, sp);

	// Region statistics from occupancy.
	double total = 0; int fluidCells = 0;
	for (int y = 0; y < t.rows; y++) for (int x = 0; x < t.cols; x++) {
		int i = std::min(x / ds, lw - 1), j = std::min(y / ds, lh - 1);
		if (!flow.isObstacle(i, j)) { total += occupancy[y * t.cols + x]; fluidCells++; }
	}
	double mean = total / std::max(1, fluidCells);
	double pit = 0, gap = 0, ridgeCol = 0; int pitN = 0;
	for (int y = 0; y < t.rows; y++) for (int x = 0; x < t.cols; x++) {
		float v = occupancy[y * t.cols + x];
		if (t.pitR > 0 && std::hypot(x - t.pitX, y - t.pitY) < t.pitR) { pit += v; pitN++; }
		if (t.gapX >= 0 && std::abs(x - t.gapX) <= 3) { ridgeCol += v; if (y >= t.gapY0 && y <= t.gapY1) gap += v; }
	}
	printf("%-13s%-16s lattice %3dx%-3d steps %lld | resets %lld nonfinite %lld | lattice %.2f ms (worst %.2f) particles %.2f ms | "
		"maxU %.3f obstacles %d flips/frame %.2f | exits/s %.0f coherence %+.2f",
		t.name.c_str(), tag.c_str(), lw, lh, flow.getSteps(), flow.getResets(), flow.getNonFinite(), latSum / statN, worstLat,
		partSum / statN, flow.getMaxSpeed(), flow.getObstacleCells(), flipSum / statN, exitSum / statN / DT, cohN ? cohSum / cohN : 0.0);
	if (pitN) printf(" | pit density / mean fluid density %.2f", (pit / pitN) / mean);
	if (ridgeCol > 0) printf(" | share of ridge-column particles in the gap %.2f", gap / ridgeCol);
	printf("\n");
	fflush(stdout);
}

int main(int argc, char ** argv)
{
	std::string out = argc > 1 ? argv[1] : ".";
	int frames = argc > 2 ? atoi(argv[2]) : 5000;

	const char * only = getenv("DEWITZ_ONLY"); // optional: run just the three spec terrains
	for (const char * name : { "flat", "mounds_ridge", "pit", "proto_layout" })
		run(boxTerrain(name), "", out, frames, boxDefaults);
	if (only) return 0;
	// Contrasts.
	run(boxTerrain("flat"), "_gradient_only", out, frames, [] { boxDefaults(); DewitzFlow::W_FLOW = 0.0f; DewitzFlow::W_GRAD = 1.0f; });
	run(boxTerrain("mounds_ridge"), "_bgk_tau198", out, frames, [] { boxDefaults(); DewitzFlow::USE_MRT = false; });
	run(boxTerrain("mounds_ridge"), "_bgk_tau055", out, frames, [] { boxDefaults(); DewitzFlow::USE_MRT = false; DewitzFlow::TAU = 0.55f; });

	if (argc > 3) {
		Terrain t{ "proto", 240, 150, 1.0f, 0.0f, std::vector<float>(240 * 150) };
		FILE * fp = fopen(argv[3], "rb");
		if (!fp || fread(t.base.data(), sizeof(float), t.base.size(), fp) != t.base.size()) { fprintf(stderr, "cannot read %s\n", argv[3]); return 1; }
		fclose(fp);
		t.pitX = 195; t.pitY = 50; t.pitR = 12; t.gapX = 115; t.gapY0 = 62; t.gapY1 = 83; t.scale = 3;
		run(t, "_bgk", out, frames, [] { protoDefaults(); DewitzFlow::USE_MRT = false; });
		run(t, "_mrt", out, frames, protoDefaults);
	}
	return 0;
}
