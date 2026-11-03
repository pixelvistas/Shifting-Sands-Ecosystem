// Headless test of the flow particles driven by the pipe model (2026-11-02).
// Compiles the real VegetationField::stepFlow()/addSourceInflow()/
// addSeepage() (copied verbatim by extract.py) and the real DewitzFlow.cpp,
// and reproduces the app's frame: elevation + sensor noise -> water
// classification with hysteresis (as VegetationField::update) -> stepFlow
// -> DewitzFlow with the water mask and the pipe velocity.
//
// Cases: channel (inlet to sink), closed basin filled by the water table,
// channel through a basin. Per case a PNG: top, a snapshot with trails
// faded like the app's FBO; bottom, a long exposure of particle positions
// over the measuring window. Plus printed metrics.
//
// Usage: ./pipe_particles_harness <outdir>
#include "stub.h"
#include "../../src/Games/DewitzFlow.h"
#include "../dewitz_harness/png.h"
#include <cstdio>
#include <random>
#include <string>

double g_frameDt = 1.0 / 40.0;
#include "gen/defaults.inc"
#include "gen/stepflow.inc"

static const int W = 519, H = 295;     // the box's grid
static const float MM_PER_CELL = 1.9f;
static const float DT = 1.0f / 40.0f;
static std::string OUT;

struct Sim {
	VegetationField v;
	DewitzFlow d;
	std::vector<float> bed, noisy;
	std::vector<float> waterState;            // hysteresis, as VegetationField::update
	std::vector<unsigned char> mask;
	std::vector<float> vx, vy;
	std::mt19937 rng{ 3 };
	std::uniform_real_distribution<float> noise{ -1.5f, 1.5f };
	Sim() : bed(W * H, 0.0f), noisy(W * H), waterState(W * H, 0.0f), mask(W * H), vx(W * H), vy(W * H) { v.init(H, W); }

	void frame() {
		for (int i = 0; i < W * H; i++) noisy[i] = bed[i] + noise(rng);
		v.previousElevation.d = noisy;
		// Water classification from last frame's depth (same order as the app).
		const float on = VegetationField::MIN_VISIBLE_WATER_DEPTH_FRAC + VegetationField::WATER_HYSTERESIS_FRAC;
		const float off = VegetationField::MIN_VISIBLE_WATER_DEPTH_FRAC;
		for (int i = 0; i < W * H; i++) {
			float dd = v.waterDepth.d[i];
			waterState[i] = (waterState[i] > 0.5f ? dd > off : dd > on) ? 1.0f : 0.0f;
		}
		v.stepFlow(0.0f);
		// WaterFlowField::updateDewitz: mask = isWaterCell, velocity = getFlowVelocityAt.
		for (int i = 0; i < W * H; i++) {
			mask[i] = waterState[i] > 0.5f ? 1 : 0;
			vx[i] = v.velX.d[i];
			vy[i] = v.velY.d[i];
		}
		d.setWaterMask(mask.data());
		d.setPipeVelocity(vx.data(), vy.data());
		d.update(noisy.data(), W, H, MM_PER_CELL, DT);
	}
};

// ------------------------------------------------------------- terrains
static float smoothstepf(float a, float b, float x) { float t = std::min(1.0f, std::max(0.0f, (x - a) / (b - a))); return t * t * (3 - 2 * t); }

// Channel centreline: gentle meander from the inlet (left) to the sink.
static float centreY(float x) { return H * 0.5f + 40.0f * std::sin(x / W * 2.0f * 3.14159f); }

static void carveChannel(Sim & s, float halfWidth, float bedLeft, float bedRight)
{
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float dy = std::fabs(y - centreY((float)x));
			float bedHere = bedLeft + (bedRight - bedLeft) * x / (W - 1);
			float depthMix = 1.0f - smoothstepf(halfWidth * 0.6f, halfWidth, dy); // sloped banks
			float e = 0.0f * (1 - depthMix) + bedHere * depthMix;
			float & b = s.bed[y * W + x];
			b = std::min(b, e);
		}
}

static void carveBowl(Sim & s, float cx, float cy, float r, float depth)
{
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float d = std::hypot(x - cx, y - cy);
			if (d < r) {
				float & b = s.bed[y * W + x];
				b = std::min(b, -depth * (1.0f - (d / r) * (d / r)));
			}
		}
}

// ------------------------------------------------------------- rendering
struct Picture {
	std::vector<float> trail, exposure; // trail: FBO-like faded; exposure: counts
	Picture() : trail(W * H, 0.0f), exposure(W * H, 0.0f) {}
	void fade(float f) { for (float & t : trail) t *= (1.0f - f); }
	void segment(float x0, float y0, float x1, float y1, float a) {
		int n = (int)std::ceil(std::max(std::fabs(x1 - x0), std::fabs(y1 - y0))) + 1;
		for (int i = 0; i <= n; i++) {
			float t = (float)i / n;
			int x = (int)(x0 + (x1 - x0) * t), y = (int)(y0 + (y1 - y0) * t);
			if (x >= 0 && x < W && y >= 0 && y < H)
				trail[y * W + x] = std::min(1.0f, trail[y * W + x] + a / (n + 1) * 2.0f);
		}
	}
	void save(const std::string & name, const Sim & s) {
		const int S = 2; // 2 image pixels per cell
		std::vector<unsigned char> img((size_t)W * S * H * 2 * S * 3);
		float maxE = 1e-6f;
		for (float e : exposure) maxE = std::max(maxE, e);
		for (int panel = 0; panel < 2; panel++)
			for (int y = 0; y < H; y++)
				for (int x = 0; x < W; x++) {
					int i = y * W + x;
					float g = 0.55f + 0.004f * std::max(-60.0f, std::min(60.0f, s.bed[i]));
					float r = g, gg = g, b = g;
					if (s.mask[i]) { // lakebed + overlay, as the shader
						float depth = std::max(0.0f, s.v.waterDepth.d[i]) * (s.v.elevationMax - s.v.elevationMin);
						float t = std::min(1.0f, depth / 14.0f); // ~MAX_WATER_DEPTH_FRAC of the range
						float a = 0.6f + 0.35f * t;
						float wr = 0.255f + (0.031f - 0.255f) * t, wg = 0.714f + (0.188f - 0.714f) * t, wb = 0.769f + (0.420f - 0.769f) * t;
						r = 0.722f * (1 - a) + wr * a; gg = 0.690f * (1 - a) + wg * a; b = 0.627f * (1 - a) + wb * a;
					}
					float add = panel == 0 ? trail[i] : std::log1p(exposure[i]) / std::log1p(maxE);
					r = std::min(1.0f, r + add); gg = std::min(1.0f, gg + add); b = std::min(1.0f, b + add * 0.9f);
					for (int sy = 0; sy < S; sy++)
						for (int sx = 0; sx < S; sx++) {
							size_t o = (((size_t)(panel * H + y) * S + sy) * W * S + (size_t)x * S + sx) * 3;
							img[o] = (unsigned char)(r * 255); img[o + 1] = (unsigned char)(gg * 255); img[o + 2] = (unsigned char)(b * 255);
						}
				}
		png::writeRGB(OUT + "/" + name + ".png", W * S, H * 2 * S, img);
	}
};

// Runs warm-up, then a measuring window; draws and collects metrics.
struct Metrics {
	double visible = 0, onWater = 0, moving = 0, frames = 0;
	double speedRatioSum = 0, speedRatioN = 0, pipeSpeedSum = 0;
	long long exitsRight = 0;
	double firstOutflowSec = -1;
	std::vector<double> xBins = std::vector<double>(10, 0.0);
};

static Metrics run(Sim & s, Picture & pic, float warmSec, float measureSec)
{
	Metrics m;
	const int warm = (int)(warmSec / DT), meas = (int)(measureSec / DT);
	for (int f = 0; f < warm + meas; f++) {
		s.frame();
		if (m.firstOutflowSec < 0 && s.v.lastOutflowRate > 0.0f) m.firstOutflowSec = f * DT;
		pic.fade(0.08f); // the app's DEWITZ_FADE default
		const auto & X = s.d.particleX(), & Y = s.d.particleY(), & PX = s.d.particlePrevX(), & PY = s.d.particlePrevY();
		const auto & A = s.d.particleAlpha();
		for (size_t k = 0; k < X.size(); k++) {
			if (A[k] <= 0.0f || (PX[k] == X[k] && PY[k] == Y[k])) continue;
			pic.segment(PX[k], PY[k], X[k], Y[k], 0.12f * A[k]);
		}
		if (f < warm) continue;
		m.frames++;
		m.exitsRight += s.d.getExitsLastFrame();
		for (size_t k = 0; k < X.size(); k++) {
			if (A[k] < 0.5f) continue;
			int x = (int)X[k], y = (int)Y[k];
			if (x < 0 || x >= W || y < 0 || y >= H) continue;
			int i = y * W + x;
			m.visible++;
			if (s.mask[i]) m.onWater++;
			pic.exposure[i] += 1.0f;
			m.xBins[std::min(9, x * 10 / W)]++;
			float pd = std::hypot(X[k] - PX[k], Y[k] - PY[k]) / DT;
			float ps = std::hypot(s.vx[i], s.vy[i]);
			if (pd > DewitzFlow::STILL_SPEED) m.moving++;
			if (ps > 3.0f) { m.speedRatioSum += pd / ps; m.speedRatioN++; m.pipeSpeedSum += ps; }
		}
	}
	return m;
}

static void report(const char * name, const Metrics & m)
{
	printf("%s\n", name);
	printf("   visible particles per frame %.0f; on water %.1f%%; moving (> STILL_SPEED) %.1f%% (%.0f per frame)\n",
		m.visible / m.frames, 100.0 * m.onWater / std::max(1.0, m.visible), 100.0 * m.moving / std::max(1.0, m.visible), m.moving / m.frames);
	if (m.speedRatioN > 0)
		printf("   particle speed / raw pipe speed at its cell (where pipe > 3 cells/s): mean %.2f; mean pipe speed there %.1f cells/s\n",
			m.speedRatioSum / m.speedRatioN, m.pipeSpeedSum / m.speedRatioN);
	printf("   particles leaving the box: %.1f per s; water first drains at the sink after %.0f s (-1 = never)\n",
		m.exitsRight / (m.frames * DT), m.firstOutflowSec);
}

int main(int argc, char ** argv)
{
	if (argc < 2) { fprintf(stderr, "usage: %s <outdir>\n", argv[0]); return 1; }
	OUT = argv[1];

	{ // 1. Channel from the inlet (left) to the sink (right).
		Sim s; Picture pic;
		carveChannel(s, 18.0f, -6.0f, -13.0f); // bed stays above the water table (-15 mm)
		VegetationField::SOURCE_SPAN = 0.12f;  // inflow only into the channel's mouth (36 cells)
		Metrics m = run(s, pic, 900.0f, 20.0f);
		pic.save("1_channel", s);
		report("1. Channel, inlet to sink (bed -6 to -13 mm, above the water table):", m);
		printf("   particle positions along the channel, by tenths of the box (left to right):");
		double tot = 0; for (double b : m.xBins) tot += b;
		for (double b : m.xBins) printf(" %.0f%%", 100.0 * b / std::max(1.0, tot));
		printf("\n");
	}
	{ // 2. Closed basin filled only by the water table.
		Sim s; Picture pic;
		float saved = VegetationField::INFLOW_MM_PER_SEC;
		VegetationField::INFLOW_MM_PER_SEC = 0.0f;
		carveBowl(s, W * 0.5f, H * 0.5f, 70.0f, 35.0f);
		Metrics m = run(s, pic, 40.0f, 20.0f);
		pic.save("2_closed_basin", s);
		VegetationField::INFLOW_MM_PER_SEC = saved;
		report("2. Closed basin (floor -35 mm), water table only, no inflow:", m);
	}
	{ // 3. Channel through a basin.
		Sim s; Picture pic;
		carveChannel(s, 18.0f, -6.0f, -13.0f);
		carveBowl(s, W * 0.5f, centreY(W * 0.5f), 60.0f, 14.0f); // floor -14 mm, just above the table
		VegetationField::SOURCE_SPAN = 0.12f;
		Metrics m = run(s, pic, 900.0f, 20.0f);
		pic.save("3_channel_through_basin", s);
		report("3. Channel through a basin (basin floor -14 mm):", m);
		// Spread inside the basin vs in the channel: particles' distance from the centreline.
		const auto & X = s.d.particleX(), & Y = s.d.particleY(), & A = s.d.particleAlpha();
		double inB = 0, inBsq = 0, nB = 0, inC = 0, inCsq = 0, nC = 0;
		double spB = 0, spC = 0;
		for (size_t k = 0; k < X.size(); k++) {
			if (A[k] < 0.5f) continue;
			float dy = Y[k] - centreY(X[k]);
			float sp = std::hypot(X[k] - s.d.particlePrevX()[k], Y[k] - s.d.particlePrevY()[k]) / DT;
			bool basin = std::hypot(X[k] - W * 0.5f, Y[k] - centreY(W * 0.5f)) < 50.0f;
			bool channel = X[k] < W * 0.25f || X[k] > W * 0.75f;
			if (basin) { inB += dy; inBsq += dy * dy; nB++; spB += sp; }
			else if (channel) { inC += dy; inCsq += dy * dy; nC++; spC += sp; }
		}
		printf("   in the basin: %.0f particles, spread (sd from centreline) %.1f cells, mean speed %.1f cells/s\n",
			nB, std::sqrt(std::max(0.0, inBsq / std::max(1.0, nB) - std::pow(inB / std::max(1.0, nB), 2))), spB / std::max(1.0, nB));
		printf("   in the channel: %.0f particles, spread %.1f cells, mean speed %.1f cells/s\n",
			nC, std::sqrt(std::max(0.0, inCsq / std::max(1.0, nC) - std::pow(inC / std::max(1.0, nC), 2))), spC / std::max(1.0, nC));
	}
	return 0;
}
