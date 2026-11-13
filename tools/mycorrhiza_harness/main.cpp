// Headless check of Prototype 2 (Mycorrhiza), style "Patches and hyphae"
// (2026-11-13). Compiles the real SuccessionModel.cpp and the module's
// plain-C++ field (MycorrhizaField.cpp, MycorrhizaFieldLayers.cpp), no
// openFrameworks, on a 519 x 295 grid at 1.9 mm per cell, 40 steps/s.
//
//   noise:  flat sand, +-2 mm uniform per cell per frame for 10 minutes ->
//           no patch, no tip (and, for the previous style, no agent).
//   ident:  SuccessionModel run twice from bare over 2,000 steps with the
//           same inputs and sweeps, once alone and once with the module
//           stepping on its outputs -> cover, M and S bit-identical.
//   bench:  cost of one step, patches and hyphae reported separately, with
//           many patches and the tip cap reached; also the previous style.
//   story:  bare sand beside a water channel. Untouched warm-up (must stay
//           black); sweep A on damp ground near the water; sweep B on dry
//           ground a few seconds later, ~60 mm away; at +60 s sweep C
//           re-digs "old" ground (soil seeded in the test set-up) and sweep D
//           digs new ground at the same moisture; then a long pause. Frames
//           are drawn as the renderer's shaders do (patch shader formulas,
//           anti-aliased hypha quads, one glow pass), at 2 px per cell.
//   quick:  a short story for tuning.
//
// Usage: ./mycorrhiza_harness <outdir> [repoRoot] [noise|ident|bench|story|quick ...] [KEY=value ...]
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
static const int PX = 2;                 // projector pixels per cell in the harness frames
static const int FW = GW * PX, FH = GH * PX;

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

// ------------------------------------------------------------ display (CPU copy of patches.frag / hypha.frag / glow.frag)
// Defaults of MycorrhizaRenderer.
static float PATCH_SCALE_MM = 40.0f;     // texture scale
static float PATCH_FLOW = 0.15f;         // flow speed
static float PATCH_CONTRAST = 1.5f;
static float PATCH_BRIGHTNESS = 1.1f;
static float EDGE_COOL = 0.7f;
static float RIM_GAIN = 0.35f;
static float STOP_POS[4] = { 0.0f, 0.33f, 0.66f, 1.0f };
static float STOP_COL[4][3] = {
	{ 0x3F / 255.f, 0xD8 / 255.f, 0xFF / 255.f },  // cyan
	{ 0xCF / 255.f, 0xEF / 255.f, 0xFF / 255.f },  // blue-white
	{ 0xFF / 255.f, 0xB3 / 255.f, 0x47 / 255.f },  // amber
	{ 0xF0 / 255.f, 0x7A / 255.f, 0x2A / 255.f } };// orange
static float HYPHA_PALE[3] = { 0xE6 / 255.f, 0xF7 / 255.f, 0xFF / 255.f };
static float HYPHA_WARM[3] = { 0xFF / 255.f, 0xE2 / 255.f, 0xB8 / 255.f };
static float WARM_GAIN = 4.0f;           // warmth (blurred presence x maturity) -> 0..1
static float HYPHA_BRIGHTNESS = 0.85f;
static float GLOW_RADIUS_PX = 2.0f;
static float GLOW_STRENGTH = 0.7f;
static float TROUGH[3] = { 0x0A / 255.f, 0x2A / 255.f, 0x6E / 255.f }; // deep blue in the swirl's troughs
static float BUMP = 3.0f;               // swirl height for the normals, in px of relief
static float SPEC_GAIN = 0.55f;
static float SHININESS = 28.0f;
static float FLOW_CYCLE = 2.0f;         // s, flow-map phase length
static float V_MAX = 30.0f;             // mm/s, velocity texture range
static float H_MAX = 12.0f;             // mm, depth texture range
static float TIME = 0.0f;

static float fractf(float x) { return x - std::floor(x); }
static float smoothstepf(float a, float b, float x) { float t = std::min(1.0f, std::max(0.0f, (x - a) / (b - a))); return t * t * (3 - 2 * t); }
// hash12 (Dave Hoskins), as in patches.frag: p3 = fract(p.xyx * .1031); p3 += dot(p3, p3.yzx + 33.33); fract((p3.x + p3.y) * p3.z).
static float hash12(float x, float y)
{
	float a = fractf(x * 0.1031f), b = fractf(y * 0.1031f), c = fractf(x * 0.1031f);
	float d = a * (b + 33.33f) + b * (c + 33.33f) + c * (a + 33.33f);
	a += d; b += d; c += d;
	return fractf((a + b) * c);
}
static float vnoise(float x, float y)
{
	float ix = std::floor(x), iy = std::floor(y), fx = x - ix, fy = y - iy;
	float ux = fx * fx * (3 - 2 * fx), uy = fy * fy * (3 - 2 * fy);
	float a = hash12(ix, iy), b = hash12(ix + 1, iy), c = hash12(ix, iy + 1), d = hash12(ix + 1, iy + 1);
	float ab = a + (b - a) * ux, cd = c + (d - c) * ux;
	return ab + (cd - ab) * uy;
}
static float fbm(float x, float y)
{
	float v = 0, amp = 0.5f;
	for (int o = 0; o < 4; o++) { v += amp * vnoise(x, y); x = x * 2.02f + 1.7f; y = y * 2.02f + 9.2f; amp *= 0.5f; }
	return v;
}
// Two octaves, normalised to 0..1 (smooth: for the marbling ridges).
static float fbm2(float x, float y)
{
	return (0.5f * vnoise(x, y) + 0.25f * vnoise(x * 2.02f + 1.7f, y * 2.02f + 9.2f)) / 0.75f;
}
// Swirl texture 0..1 at grid cell coordinate (cx, cy): curl-advected, two-level domain warp.
static float WARP = 2.5f;          // domain-warp strength
static float BANDS = 6.0f;         // marbling ridges per unit of the warped field
static float RIDGE_WIDTH = 0.35f;  // ridge width (fraction of a band)
static float STREAK = 0.45f;       // tap spacing along the curl direction (streaks)
static float swirl(float cx, float cy, float k = 0.0f)
{
	const float t = TIME * PATCH_FLOW;
	const float bx = cx * MM / PATCH_SCALE_MM + 17.3f * k, by = cy * MM / PATCH_SCALE_MM + 9.1f * k;
	const float e = 0.1f, cpx = bx * 0.5f + 3.1f + 0.05f * t, cpy = by * 0.5f + 7.7f;
	const float curlX = (vnoise(cpx, cpy + e) - vnoise(cpx, cpy - e)) / (2 * e);
	const float curlY = -(vnoise(cpx + e, cpy) - vnoise(cpx - e, cpy)) / (2 * e);
	const float px = bx + 0.5f * curlX, py = by + 0.5f * curlY;
	const float qx = fbm(px + 0.10f * t, py), qy = fbm(px + 5.2f, py + 1.3f - 0.08f * t);
	const float rx = fbm(px + WARP * qx + 1.7f + 0.12f * t, py + WARP * qy + 9.2f);
	const float ry = fbm(px + WARP * qx + 8.3f, py + WARP * qy + 2.8f - 0.10f * t);
	float dl = std::sqrt(curlX * curlX + curlY * curlY);
	const float dx = dl > 1e-4f ? curlX / dl : 1.0f, dy = dl > 1e-4f ? curlY / dl : 0.0f;
	const float ux = px + WARP * rx, uy = py + WARP * ry;
	// Body: the warped 4-octave field. Ridges: bright lines along the iso-contours of a smooth
	// 2-octave field, averaged along the curl direction (streaks) - marbling.
	const float v = fbm(ux, uy);
	const float vr = (fbm2(ux - dx * STREAK, uy - dy * STREAK) + fbm2(ux, uy) + fbm2(ux + dx * STREAK, uy + dy * STREAK)) / 3.0f;
	const float body = std::min(1.0f, std::max(0.0f, 0.5f + PATCH_CONTRAST * (v - 0.5f)));
	const float tri = 1.0f - std::fabs(2.0f * fractf(vr * BANDS) - 1.0f);
	const float ridge = smoothstepf(1.0f - RIDGE_WIDTH, 1.0f, tri);
	return std::min(1.0f, 0.3f + 0.4f * body + 0.45f * ridge);
}
static void ramp4(float m, float out[3])
{
	m = std::min(1.0f, std::max(0.0f, m));
	int s = 0;
	while (s < 2 && m > STOP_POS[s + 1]) s++;
	float u = std::min(1.0f, std::max(0.0f, (m - STOP_POS[s]) / std::max(1e-4f, STOP_POS[s + 1] - STOP_POS[s])));
	for (int k = 0; k < 3; k++) out[k] = STOP_COL[s][k] + (STOP_COL[s + 1][k] - STOP_COL[s][k]) * u;
}
// Texel lookups as GL_LINEAR on a rectangle texture of 8-bit values (the renderer uploads presence and maturity as RGBA8).
static float q8(float v) { return std::lround(std::min(1.0f, std::max(0.0f, v)) * 255.0f) / 255.0f; }
static float bilin(const float * f, float x, float y)
{
	x -= 0.5f; y -= 0.5f;
	int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
	float tx = x - x0, ty = y - y0;
	auto at = [&](int xi, int yi) { xi = std::max(0, std::min(GW - 1, xi)); yi = std::max(0, std::min(GH - 1, yi)); return q8(f[(size_t)yi * GW + xi]); };
	return (at(x0, y0) * (1 - tx) + at(x0 + 1, y0) * tx) * (1 - ty) + (at(x0, y0 + 1) * (1 - tx) + at(x0 + 1, y0 + 1) * tx) * ty;
}

// Half-resolution liquid fields as uploaded (8 bit), sampled bilinearly at half-grid coordinate (lx, ly).
static float bilinHalf(const std::vector<float> & f, int LW, int LH, float lx, float ly)
{
	lx -= 0.5f; ly -= 0.5f;
	int x0 = (int)std::floor(lx), y0 = (int)std::floor(ly);
	float tx = lx - x0, ty = ly - y0;
	auto at = [&](int xi, int yi) { xi = std::max(0, std::min(LW - 1, xi)); yi = std::max(0, std::min(LH - 1, yi)); return f[(size_t)yi * LW + xi]; };
	return (at(x0, y0) * (1 - tx) + at(x0 + 1, y0) * tx) * (1 - ty) + (at(x0, y0 + 1) * (1 - tx) + at(x0 + 1, y0 + 1) * tx) * ty;
}
// Patch layer over black (patches.frag): swirl advected along the liquid's
// velocity (flow map: two phases, cross-faded), lit with normals from the
// pixel derivatives of the swirl height (dFdx/dFdy), diffuse + specular;
// colour by maturity, warmer where the liquid is deep, cooler at edges,
// thin rim; brightness proportional to presence.
static void drawPatches(const MF & f, std::vector<float> & rgb)
{
	const float * P = f.presence();
	const float * M = f.maturity();
	const int LW = f.getLiquidCols(), LH = f.getLiquidRows();
	std::vector<float> vxq((size_t)LW * LH), vyq(vxq.size()), dq(vxq.size());
	for (size_t l = 0; l < vxq.size(); l++) { // as the renderer encodes them
		vxq[l] = (q8(0.5f + f.liquidVelX()[l] / (2 * V_MAX)) - 0.5f) * 2 * V_MAX;
		vyq[l] = (q8(0.5f + f.liquidVelY()[l] / (2 * V_MAX)) - 0.5f) * 2 * V_MAX;
		dq[l] = q8(f.liquidDepth()[l] / H_MAX) * H_MAX;
	}
	const float ph0 = fractf(TIME / FLOW_CYCLE), ph1 = fractf(TIME / FLOW_CYCLE + 0.5f), blend = std::fabs(1.0f - 2.0f * ph0);
	std::vector<float> texBuf((size_t)FW * FH, -1.0f), pBuf((size_t)FW * FH, 0.0f);
	auto texAt = [&](int x, int y) -> float {
		float & t = texBuf[(size_t)y * FW + x];
		if (t >= 0.0f) return t;
		const float cx = (x + 0.5f) / PX, cy = (y + 0.5f) / PX;
		const float vx = bilinHalf(vxq, LW, LH, cx * 0.5f, cy * 0.5f) / MM, vy = bilinHalf(vyq, LW, LH, cx * 0.5f, cy * 0.5f) / MM;
		const float s0 = swirl(cx - vx * ph0 * FLOW_CYCLE, cy - vy * ph0 * FLOW_CYCLE, 0.0f);
		const float s1 = swirl(cx - vx * ph1 * FLOW_CYCLE, cy - vy * ph1 * FLOW_CYCLE, 1.0f);
		t = s0 + (s1 - s0) * blend;
		return t;
	};
	const float Lx = -0.4f, Ly = -0.5f, Lz = 0.77f; // light, normalised below
	const float ll = std::sqrt(Lx * Lx + Ly * Ly + Lz * Lz);
	const float lx = Lx / ll, ly = Ly / ll, lz = Lz / ll;
	float hx = lx, hy = ly, hz = lz + 1.0f; { float hl = std::sqrt(hx * hx + hy * hy + hz * hz); hx /= hl; hy /= hl; hz /= hl; }
	for (int y = 0; y < FH; y++)
		for (int x = 0; x < FW; x++) {
			const float cx = (x + 0.5f) / PX, cy = (y + 0.5f) / PX;
			const float p = bilin(P, cx, cy);
			if (p < 0.004f) continue;
			const float m = bilin(M, cx, cy);
			const float gx = bilin(P, cx + 1, cy) - bilin(P, cx - 1, cy), gy = bilin(P, cx, cy + 1) - bilin(P, cx, cy - 1);
			const float edge = smoothstepf(0.02f, 0.15f, 0.5f * std::sqrt(gx * gx + gy * gy));
			const float tex = texAt(x, y);
			// normals from the screen-space derivatives of the height (as dFdx / dFdy)
			const float H0 = tex * BUMP;
			const float ddx = (x + 1 < FW ? texAt(x + 1, y) : tex) * BUMP - H0, ddy = (y + 1 < FH ? texAt(x, y + 1) : tex) * BUMP - H0;
			float nx = -ddx, ny = -ddy, nz = 1.0f; { float nl = std::sqrt(nx * nx + ny * ny + nz * nz); nx /= nl; ny /= nl; nz /= nl; }
			const float diff = 0.6f + 0.4f * std::max(0.0f, nx * lx + ny * ly + nz * lz);
			const float spec = SPEC_GAIN * std::pow(std::max(0.0f, nx * hx + ny * hy + nz * hz), SHININESS);
			const float depth = bilinHalf(dq, LW, LH, cx * 0.5f, cy * 0.5f);
			const float deep = 0.75f + 0.45f * smoothstepf(0.0f, 0.5f * H_MAX, depth);
			const float mEff = std::min(1.0f, std::max(0.0f, m * (1.0f - EDGE_COOL * edge) * (0.35f + 0.9f * tex) * deep));
			float c[3]; ramp4(mEff, c);
			const float tr = smoothstepf(0.15f, 0.6f, tex);
			for (int k = 0; k < 3; k++) c[k] = TROUGH[k] + (c[k] - TROUGH[k]) * tr;
			const float bright = 0.35f + 0.85f * tex;
			const float rim = RIM_GAIN * std::exp(-((p - 0.5f) / 0.12f) * ((p - 0.5f) / 0.12f));
			const float a = std::min(1.0f, p * 1.2f) * PATCH_BRIGHTNESS;
			float * o = &rgb[((size_t)y * FW + x) * 3];
			for (int k = 0; k < 3; k++) o[k] += (c[k] * bright * diff + (spec + rim) * STOP_COL[1][k]) * a;
		}
}
// Hyphae: each segment (and each growing tip's partial segment) as a quad
// width w each side of the line, intensity 1 at the line falling linearly
// to 0 at distance w (hypha.vert/.frag), additive; then glow.frag: a ring
// blur (8 taps at r, 8 at 2r) added x GLOW_STRENGTH.
static void drawHyphae(const MF & f, std::vector<float> & rgb)
{
	std::vector<float> h((size_t)FW * FH * 3, 0.0f);
	const int bwN = f.getBlockCols(), bhN = f.getBlockRows();
	const int B = bwN > 0 ? (GW + bwN - 1) / bwN : 4;
	auto seg = [&](float x0, float y0, float x1, float y1, float w, float birth) {
		if (w <= 0.0f) return;
		const float b = f.fadeAt(birth) * HYPHA_BRIGHTNESS;
		if (b <= 0.0f) return;
		const float mx = 0.5f * (x0 + x1), my = 0.5f * (y0 + y1);
		// warmth at the segment (warmthBlocks sampled bilinearly, as the shader samples its texture)
		const float fx = mx / B - 0.5f, fy = my / B - 0.5f;
		int bx0 = (int)std::floor(fx), by0 = (int)std::floor(fy); float tx = fx - bx0, ty = fy - by0;
		// as uploaded: 8 bit, min(1, WARM_GAIN x warmth), sampled bilinearly
		auto wb = [&](int bx, int by) { bx = std::max(0, std::min(bwN - 1, bx)); by = std::max(0, std::min(bhN - 1, by)); return q8(WARM_GAIN * f.warmthBlocks()[(size_t)by * bwN + bx]); };
		const float warm = (wb(bx0, by0) * (1 - tx) + wb(bx0 + 1, by0) * tx) * (1 - ty) + (wb(bx0, by0 + 1) * (1 - tx) + wb(bx0 + 1, by0 + 1) * tx) * ty;
		float col[3]; for (int k = 0; k < 3; k++) col[k] = (HYPHA_PALE[k] + (HYPHA_WARM[k] - HYPHA_PALE[k]) * warm) * b;
		x0 *= PX; y0 *= PX; x1 *= PX; y1 *= PX;
		const float dx = x1 - x0, dy = y1 - y0, L2 = dx * dx + dy * dy;
		for (int y = std::max(0, (int)std::floor(std::min(y0, y1) - w)); y <= std::min(FH - 1, (int)std::ceil(std::max(y0, y1) + w)); y++)
			for (int x = std::max(0, (int)std::floor(std::min(x0, x1) - w)); x <= std::min(FW - 1, (int)std::ceil(std::max(x0, x1) + w)); x++) {
				const float px = x + 0.5f, py = y + 0.5f;
				float t = L2 > 0 ? ((px - x0) * dx + (py - y0) * dy) / L2 : 0.0f;
				t = std::min(1.0f, std::max(0.0f, t));
				const float d = std::hypot(px - (x0 + t * dx), py - (y0 + t * dy));
				const float a = 1.0f - d / w;
				if (a <= 0) continue;
				float * o = &h[((size_t)y * FW + x) * 3];
				for (int k = 0; k < 3; k++) o[k] = std::max(o[k], col[k] * a); // the joints of one line do not double up
			}
	};
	for (const MF::Segment & s : f.segments()) seg(s.x0, s.y0, s.x1, s.y1, s.width, s.birth);
	for (const MF::Tip & t : f.tips()) seg(t.sx, t.sy, t.x, t.y, t.width, (float)f.getTime());
	static const float D[8][2] = { {1,0},{0.7071f,0.7071f},{0,1},{-0.7071f,0.7071f},{-1,0},{-0.7071f,-0.7071f},{0,-1},{0.7071f,-0.7071f} };
	auto at = [&](float x, float y, int k) { int xi = std::max(0, std::min(FW - 1, (int)std::lround(x))), yi = std::max(0, std::min(FH - 1, (int)std::lround(y))); return h[((size_t)yi * FW + xi) * 3 + k]; };
	for (int y = 0; y < FH; y++)
		for (int x = 0; x < FW; x++)
			for (int k = 0; k < 3; k++) {
				float g = 0;
				for (int d = 0; d < 8; d++) {
					g += at(x + D[d][0] * GLOW_RADIUS_PX, y + D[d][1] * GLOW_RADIUS_PX, k);
					g += 0.5f * at(x + D[d][0] * 2 * GLOW_RADIUS_PX, y + D[d][1] * 2 * GLOW_RADIUS_PX, k);
				}
				rgb[((size_t)y * FW + x) * 3 + k] += h[((size_t)y * FW + x) * 3 + k] + GLOW_STRENGTH * g / 12.0f;
			}
}
static std::vector<unsigned char> renderFrame(const MF & f)
{
	std::vector<float> rgb((size_t)FW * FH * 3, 0.0f);
	drawPatches(f, rgb);
	drawHyphae(f, rgb);
	std::vector<unsigned char> img(rgb.size());
	for (size_t i = 0; i < rgb.size(); i++) img[i] = (unsigned char)std::lround(std::min(1.0f, rgb[i]) * 255.0f);
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
	float handRadiusMM = 35.0f, digRateMM = 40.0f, maxDigMM = 14.0f, occludeRadiusMM = 50.0f;
	World() : bed(GW * GH, FLAT_MM), dug(GW * GH, 0.0f), elev(GW * GH), above(GW * GH), water(GW * GH, 0) {}
	// Flat sand with a water channel along the bottom; the ground falls toward it (damp banks).
	void channel() {
		for (int y = 0; y < GH; y++) for (int x = 0; x < GW; x++) {
			float yc = 0.86f * GH + 6.0f * std::sin(x / 60.0f);
			float d = std::fabs(y - yc) * MM;
			float h = FLAT_MM - 20.0f * std::exp(-d / 70.0f);
			if (std::fabs(y - yc) < 8.0f) { h = -24.0f; water[y * GW + x] = 1; }
			bed[y * GW + x] = h;
		}
	}
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
		if (h) { // the hand digs under it
			int r = (int)std::ceil(handRadiusMM / MM);
			for (int y = std::max(0, (int)hy - r); y <= std::min(GH - 1, (int)hy + r); y++)
				for (int x = std::max(0, (int)hx - r); x <= std::min(GW - 1, (int)hx + r); x++) {
					float d = std::hypot(x - hx, y - hy) * MM;
					if (d > handRadiusMM || water[(size_t)y * GW + x]) continue;
					size_t i = (size_t)y * GW + x;
					float target = maxDigMM * std::min(1.0f, 1.6f * (1.0f - d / handRadiusMM));
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
					if (std::hypot(x - hx, y - hy) * MM <= occludeRadiusMM * 0.6f) elev[(size_t)y * GW + x] = NAN;
		}
	}
	SM::Inputs smInputs() {
		SM::Inputs in;
		in.elevationMM = elev.data(); in.water = water.data(); in.heightAboveTableMM = above.data();
		in.cols = GW; in.rows = GH; in.mmPerCell = MM; in.dt = DT;
		in.temperature = 0.745f; in.restingTemperature = 0.745f; in.spreadRadiusMM = 40.0f;
		return in;
	}
	static MF::Inputs mfInputs(const World & w, const SM * m) {
		MF::Inputs in;
		in.elevationMM = w.elev.data();
		bool ok = m && m->getCols() == GW && m->getRows() == GH;
		for (int r = 0; r < 3; r++) in.cover[r] = ok ? m->cover(r) : nullptr;
		in.soil = ok ? m->soil() : nullptr;
		in.moisture = ok ? m->moisture() : nullptr;
		in.water = w.water.data();
		in.cols = GW; in.rows = GH; in.mmPerCell = MM; in.dt = DT;
		return in;
	}
};

// ------------------------------------------------------------ checks
static bool checkNoise()
{
	bool ok = true;
	for (int style = 0; style < 2; style++) {
		MF::STYLE = style;
		World w; w.noiseMM = 2.0f;
		MF f; f.clear();
		float worst = 0.0f; int touched = 0; double presence = 0;
		const int steps = 10 * 60 * 40;
		for (int s = 0; s < steps; s++) {
			w.sample(s * DT);
			f.step(World::mfInputs(w, nullptr));
			worst = std::max(worst, f.maxTouchChangeMM());
			touched += f.touchedBlocks();
		}
		for (int i = 0; i < GW * GH; i++) presence += f.presence()[i];
		printf("noise (%s): +-2 mm uniform per cell per frame, flat, 10 min: touched block-steps %d, largest block change %.3f mm "
			"(threshold %.1f); agents spawned %lld, tips spawned %lld, presence sum %.3g, segments %zu\n",
			style ? "Physarum (previous)" : "Patches and hyphae", touched, worst, MF::TOUCH_MM, f.spawnedTotal(),
			f.hyphaTipsSpawned(), presence, f.segments().size());
		ok = ok && touched == 0 && f.spawnedTotal() == 0 && f.hyphaTipsSpawned() == 0 && presence == 0.0;
	}
	MF::STYLE = MF::STYLE_PATCHES_HYPHAE;
	return ok;
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
	World wa, wb; wa.channel(); wb.channel();
	std::vector<Stroke> st = { { 5, 2.5f, 80, 200, 180, 210 }, { 20, 2.5f, 200, 120, 300, 110 } };
	wa.strokes = wb.strokes = st;
	SM a, b; a.setParams(PRESET, err); b.setParams(PRESET, err);
	MF f; f.clear();
	bool same = true;
	for (int s = 0; s < 2000; s++) {
		wa.sample(s * DT); wb.sample(s * DT);
		SM::Inputs ia = wa.smInputs(), ib = wb.smInputs();
		a.step(ia); b.step(ib);
		f.step(World::mfInputs(wb, &b)); // module on run b only
		if ((s % 100 == 99 || s == 1999) && hashModel(a) != hashModel(b)) same = false;
	}
	const size_t n = (size_t)GW * GH * sizeof(float);
	bool bytes = std::memcmp(a.cover(0), b.cover(0), n) == 0 && std::memcmp(a.cover(1), b.cover(1), n) == 0 &&
		std::memcmp(a.cover(2), b.cover(2), n) == 0 && std::memcmp(a.moisture(), b.moisture(), n) == 0 &&
		std::memcmp(a.soil(), b.soil(), n) == 0;
	printf("ident: 2,000 steps, channel + 2 sweeps; module tips spawned %lld, segments %zu; cover/M/S hash alone %016llx, "
		"with module %016llx; bit-identical at every 100-step check: %s; final memcmp: %s\n",
		f.hyphaTipsSpawned(), f.segments().size(), (unsigned long long)hashModel(a), (unsigned long long)hashModel(b),
		same ? "yes" : "NO", bytes ? "identical" : "DIFFERENT");
	return same && bytes;
}

static void stats(const char * name, std::vector<float> v)
{
	if (v.empty()) { printf("  %s: no samples\n", name); return; }
	std::sort(v.begin(), v.end());
	double s = 0; for (float x : v) s += x;
	printf("  %s: mean %.2f ms, median %.2f, p95 %.2f, worst %.2f\n", name, s / v.size(), v[v.size() / 2], v[(size_t)(0.95 * (v.size() - 1))], v.back());
}
static void bench(int threads)
{
	const int savedThreads = MF::THREADS; MF::THREADS = threads;
	{ // Patches and hyphae: twelve sweeps over the box, then 60 s of growth.
		World w; w.channel();
		for (int k = 0; k < 12; k++) {
			float y = 30.0f + (k % 6) * 38.0f, x0 = (k < 6) ? 30.0f : 280.0f;
			w.strokes.push_back({ 1.0f + 0.6f * k, 2.0f, x0, y, x0 + 200.0f, y + 10.0f });
		}
		MF f; f.clear();
		std::vector<float> touch, patches, attract, hyphae, total;
		size_t maxSeg = 0, maxTips = 0;
		for (int s = 0; s < 70 * 40; s++) {
			w.sample(s * DT);
			f.step(World::mfInputs(w, nullptr));
			maxSeg = std::max(maxSeg, f.segments().size()); maxTips = std::max(maxTips, f.tips().size());
			if (s * DT > 5.0f) {
				touch.push_back(f.getPhaseMs(0)); patches.push_back(f.getPhaseMs(1)); attract.push_back(f.getPhaseMs(2));
				hyphae.push_back(f.getPhaseMs(3)); total.push_back(f.getLastStepMs());
			}
		}
		printf("bench (Patches and hyphae, THREADS = %d, -O2, 12 sweeps, 65 s timed): peak tips %zu (cap %d), peak segments %zu (cap %d)\n",
			threads, maxTips, MF::MAX_TIPS, maxSeg, MF::MAX_SEGMENTS);
		stats("touch", touch); stats("patches", patches); stats("attractant + labels", attract); stats("hyphae", hyphae); stats("whole step", total);
	}
	{ // Previous style at 100,000 agents.
		MF::STYLE = MF::STYLE_PHYSARUM;
		World w;
		MF f; f.clear();
		const int savedCap = MF::MAX_AGENTS; MF::MAX_AGENTS = 100000;
		std::vector<float> ms;
		for (int s = 0; s < 1200; s++) {
			float t = s * DT;
			w.sample(t);
			float lift = s < 200 ? 15.0f * std::sin(t * 6.0f) : 0.0f;
			for (float & e : w.elev) e += lift;
			f.step(World::mfInputs(w, nullptr));
			if (f.agentCount() >= 100000) ms.push_back(f.getLastStepMs());
		}
		printf("bench (Physarum (previous), THREADS = %d, 100,000 agents):\n", threads);
		stats("whole step", ms);
		MF::MAX_AGENTS = savedCap;
		MF::STYLE = MF::STYLE_PATCHES_HYPHAE;
	}
	MF::THREADS = savedThreads;
}

static void savePNG(const std::string & name, const std::vector<unsigned char> & img, int w = FW, int h = FH)
{
	png::writeRGB(OUT + "/" + name, w, h, img);
}

// Cells a sweep dug (more than 3 mm since the previous sweep ended).
struct Region { std::vector<int> cells; };
static Region dugRegion(const World & w, const std::vector<float> & dugBefore)
{
	Region r;
	for (int i = 0; i < GW * GH; i++) if (w.dug[i] - dugBefore[i] > 3.0f) r.cells.push_back(i);
	return r;
}
static float meanOver(const float * f, const Region & r) { double s = 0; for (int i : r.cells) s += f[i]; return r.cells.empty() ? 0.0f : (float)(s / r.cells.size()); }

static void story(bool quick)
{
	std::string err;
	World w; w.channel();
	SM m; m.setParams(PRESET, err);
	MF f; f.clear();
	const float WARM = quick ? 30.0f : 120.0f;
	// Sweeps (cells): A damp, near the channel; B dry, ~60 mm (edge to edge) above A; C re-dig of "old" ground; D new ground.
	const float yA = 0.74f * GH, yB = yA - (60.0f + 2.0f * 25.0f) / MM;
	// B sits on a raised plateau (+30 mm, smooth edges): dry ground, far above the water table.
	for (int y = 0; y < GH; y++) for (int x = 0; x < GW; x++) {
		const float dy = std::fabs(y - yB) * MM, dx = std::max(0.0f, std::max(0.18f * GW - x, x - 0.46f * GW)) * MM;
		const float d = std::max(0.0f, std::hypot(dx, std::max(0.0f, dy - 30.0f)));
		if (!w.water[(size_t)y * GW + x]) w.bed[(size_t)y * GW + x] += 30.0f * std::exp(-d * d / (2 * 20.0f * 20.0f));
	}
	w.strokes = {
		{ WARM + 0.0f, 2.5f, 0.12f * GW, yA, 0.32f * GW, yA - 6 },
		{ WARM + 5.0f, 2.5f, 0.22f * GW, yB, 0.42f * GW, yB - 4 },
		{ WARM + 60.0f, 2.0f, 0.60f * GW, 0.30f * GH, 0.72f * GW, 0.30f * GH },
		{ WARM + 63.0f, 2.0f, 0.80f * GW, 0.30f * GH, 0.92f * GW, 0.30f * GH } };
	const Stroke C = w.strokes[2];
	const float END = WARM + 70.0f + (quick ? 40.0f : 480.0f);
	const int steps = (int)(END / DT);
	int frame = 0;
	bool blackBefore = true;
	long long tipsBefore = 0;
	std::vector<float> dugBefore = w.dug;
	std::vector<int> flooded;
	const float floodProbe[4] = { 0.0f, 10.0f, 30.0f, 60.0f };
	float floodLit[4] = { -1, -1, -1, -1 };
	Region reg[4];
	const float marks[4] = { WARM + 2.5f, WARM + 7.5f, WARM + 62.0f, WARM + 65.0f };
	const float probes[5] = { 5, 10, 20, 40, 60 };
	float mat[4][5] = {};
	float half[4] = { -1, -1, -1, -1 };
	float lightAt = -1, iouA = 0;
	std::vector<float> stepMs[4];
	double shown[5] = {};
	const float pauseT = WARM + 70.0f;
	const float fadeProbe[5] = { 0, 60, 120, 300, 450 };
	for (int s = 0; s < steps; s++) {
		const float t = s * DT;
		w.sample(t);
		SM::Inputs si = w.smInputs();
		m.step(si);
		if (s == 0) { // test set-up: "old" ground under sweep C has built soil and cover (S 0.8, cover 0.5)
			for (int y = 0; y < GH; y++) for (int x = 0; x < GW; x++) {
				if (x < C.x0 - 25 || x > C.x1 + 25 || std::fabs(y - C.y0) > 30) continue;
				const size_t i = (size_t)y * GW + x;
				m.soilMutable()[i] = 0.8f;
				m.coverMutable(SM::INTERMEDIATE)[i] = 0.5f;
			}
		}
		if (s == (int)std::lround(w.strokes[3].t0 / DT)) { // test set-up: sweep D brings new fill (no soil, no cover)
			const Stroke & D = w.strokes[3];
			for (int y = 0; y < GH; y++) for (int x = 0; x < GW; x++) {
				if (x < D.x0 - 20 || x > D.x1 + 20 || std::fabs(y - D.y0) > 22) continue;
				const size_t i = (size_t)y * GW + x;
				m.soilMutable()[i] = 0.0f;
				for (int r = 0; r < 3; r++) m.coverMutable(r)[i] = 0.0f;
			}
		}
		if (s == (int)std::lround((WARM + 5.5f) / DT)) { // test set-up: the water table fills A's deepest part (dug > 10 mm)
			for (int i = 0; i < GW * GH; i++) if (w.dug[i] > 10.0f && std::fabs(i / GW - yA) < 40) { w.water[i] = 1; flooded.push_back(i); }
		}
		f.step(World::mfInputs(w, &m));
		for (int p = 0; p < 4; p++)
			if (!flooded.empty() && s == (int)std::lround((WARM + 5.5f + floodProbe[p]) / DT)) {
				int lit = 0; for (int i : flooded) lit += f.presence()[i] > 0.5f;
				floodLit[p] = (float)lit / flooded.size();
			}
		if (t < WARM) tipsBefore = f.hyphaTipsSpawned();
		if (t >= WARM) for (int p = 0; p < 4; p++) stepMs[p].push_back(p < 3 ? f.getPhaseMs(p + 1) : f.getLastStepMs());
		for (int k = 0; k < 4; k++)
			if (s == (int)std::lround(marks[k] / DT)) { reg[k] = dugRegion(w, dugBefore); dugBefore = w.dug; }
		if (t >= WARM + 2.5f && t < WARM + 8.0f && lightAt < 0) { // sweep A lit: > 50 % of its dug cells at presence > 0.5
			int dugN = 0, lit = 0;
			for (int i = 0; i < GW * GH; i++) if (w.dug[i] > 3.0f) { dugN++; if (f.presence()[i] > 0.5f) lit++; }
			if (dugN > 50 && lit > dugN / 2) lightAt = t - (WARM + 2.5f);
		}
		if (s == (int)std::lround((WARM + 3.5f) / DT)) { // IoU, 1 s after sweep A ends
			int inter = 0, uni = 0;
			for (int i = 0; i < GW * GH; i++) { bool a = w.dug[i] > 3.0f, b = f.presence()[i] > 0.5f; inter += a && b; uni += a || b; }
			iouA = uni ? (float)inter / uni : 0;
		}
		for (int k = 0; k < 4; k++) {
			if (reg[k].cells.empty()) continue;
			const float since = t - marks[k];
			const float mv = meanOver(f.maturity(), reg[k]);
			for (int p = 0; p < 5; p++) if (s == (int)std::lround((marks[k] + probes[p]) / DT)) mat[k][p] = mv;
			if (half[k] < 0 && since > 0 && mv >= 0.5f) half[k] = since;
		}
		for (int p = 0; p < 5; p++)
			if (s == (int)std::lround((pauseT + fadeProbe[p]) / DT)) {
				double sum = 0;
				for (int i = 0; i < GW * GH; i++) sum += f.presence()[i];
				for (const MF::Segment & g : f.segments()) if (g.width > 0) sum += f.fadeAt(g.birth) * std::hypot(g.x1 - g.x0, g.y1 - g.y0);
				shown[p] = sum;
			}
		// Frames: every 15 s before touch; every 0.5 s for the first 12 s, 1 s to +70 s, then 10 s.
		const float u = t - WARM;
		bool shoot = (u < 0 && s % (15 * 40) == 0) || (u >= 0 && u < 12 && s % 20 == 0) || (u >= 12 && u < 70 && s % 40 == 0) ||
			(u >= 70 && s % 400 == 0);
		if (quick) shoot = (u < 0 && s % (15 * 40) == 0) || (u >= 0 && s % 80 == 0);
		if (!shoot) continue;
		TIME = t;
		auto img = renderFrame(f);
		if (u < 0) for (unsigned char c : img) if (c) { blackBefore = false; break; }
		if (std::fabs(u - 24.0f) < 0.01f) savePNG("still.png", img);
		if (std::fabs(u - 80.0f) < 0.01f) savePNG("still_late.png", img); // on the 10 s grid
		// GIF annotation only (not projected): a grey ring where the simulated hand is.
		float hx, hy;
		if (w.hand(t, hx, hy)) {
			const float r = w.handRadiusMM / MM * PX;
			for (int a = 0; a < 720; a++) {
				int px = (int)std::lround(hx * PX + r * std::cos(a * 0.008727f)), py = (int)std::lround(hy * PX + r * std::sin(a * 0.008727f));
				if (px < 0 || py < 0 || px >= FW || py >= FH) continue;
				unsigned char * q = &img[((size_t)py * FW + px) * 3];
				q[0] = q[1] = q[2] = 150;
			}
		}
		char name[64]; snprintf(name, sizeof name, "story_%04d.png", frame++);
		savePNG(name, img);
	}
	auto mean = [](const std::vector<float> & v) { double s = 0; for (float x : v) s += x; return v.empty() ? 0.0 : s / v.size(); };
	auto worst = [](const std::vector<float> & v) { float m = 0; for (float x : v) m = std::max(m, x); return m; };
	printf("story%s: %d steps (%.0f s), warm-up %.0f s untouched: frames black %s, tips spawned before touch %lld\n",
		quick ? " (quick)" : "", steps, END, WARM, blackBefore ? "yes" : "NO", tipsBefore);
	printf("story: sweep A lit (> 50 %% of its dug cells at presence > 0.5) %.2f s after the sweep ended; IoU lit vs dug 1 s after: %.2f\n", lightAt, iouA);
	const char * nm[4] = { "A damp (near water)", "B dry", "C re-dug old ground", "D new ground" };
	for (int k = 0; k < 4; k++) {
		float Mo = 0, So = 0;
		for (int i : reg[k].cells) { Mo += m.moisture()[i]; So += m.soil()[i]; }
		if (!reg[k].cells.empty()) { Mo /= reg[k].cells.size(); So /= reg[k].cells.size(); }
		printf("story: %-20s cells %5zu (M %.2f, S %.2f at the end), mean maturity +5/+10/+20/+40/+60 s: %.2f %.2f %.2f %.2f %.2f; 0.5 after %s\n",
			nm[k], reg[k].cells.size(), Mo, So, mat[k][0], mat[k][1], mat[k][2], mat[k][3], mat[k][4],
			half[k] < 0 ? "(not within the run)" : (std::to_string((int)std::lround(half[k])) + " s").c_str());
	}
	printf("story: A's deepest part turned to water at +5.5 s (%zu cells): lit (presence > 0.5) at +0/+10/+30/+60 s: %.0f%% %.0f%% %.0f%% %.0f%%\n",
		flooded.size(), 100 * floodLit[0], 100 * floodLit[1], 100 * floodLit[2], 100 * floodLit[3]);
	printf("story: hyphae: tips spawned %lld, fusions %lld, connections to another patch %lld, segments now %zu\n",
		f.hyphaTipsSpawned(), f.hyphaFusions(), f.hyphaPatchConnections(), f.segments().size());
	double onWater = 0;
	for (int i = 0; i < GW * GH; i++) if (w.water[i]) onWater += f.presence()[i];
	int segWet = 0;
	for (const MF::Segment & g : f.segments())
		if (g.width > 0 && (w.water[(size_t)(int)g.y0 * GW + (int)g.x0] || w.water[(size_t)(int)g.y1 * GW + (int)g.x1])) segWet++;
	printf("story: on water at the end: presence sum %.3g, live segments touching water %d\n", onWater, segWet);
	if (!quick)
		printf("story: lit amount (presence + live segment length x brightness) at the pause %.0f; +1 min %.0f%%, +2 min %.0f%%, +5 min %.0f%%, +7.5 min %.0f%%\n",
			shown[0], 100 * shown[1] / std::max(1.0, shown[0]), 100 * shown[2] / std::max(1.0, shown[0]), 100 * shown[3] / std::max(1.0, shown[0]),
			100 * shown[4] / std::max(1.0, shown[0]));
	printf("story: step after first touch (with SuccessionModel inputs): patches %.2f ms (worst %.2f), attractant %.2f, hyphae %.2f (worst %.2f), whole %.2f (worst %.2f)\n",
		mean(stepMs[0]), worst(stepMs[0]), mean(stepMs[1]), mean(stepMs[2]), worst(stepMs[2]), mean(stepMs[3]), worst(stepMs[3]));
}

// KEY=value overrides of the field's tunables and the display copy.
static bool setParam(const std::string & kv)
{
	size_t e = kv.find('=');
	if (e == std::string::npos) return false;
	std::string k = kv.substr(0, e); float v = (float)atof(kv.c_str() + e + 1);
	struct F { const char * n; float * p; } fs[] = {
		{"PATCH_CELL_MM",&MF::PATCH_CELL_MM},{"LIQUID_SOURCE_MM_S",&MF::LIQUID_SOURCE_MM_S},{"LIQUID_VISCOSITY",&MF::LIQUID_VISCOSITY},{"LIQUID_PRESENCE_MM",&MF::LIQUID_PRESENCE_MM},{"HYPHA_TOUCH_BOOST",&MF::HYPHA_TOUCH_BOOST},{"BUMP",&BUMP},{"WARP",&WARP},{"BANDS",&BANDS},{"RIDGE_WIDTH",&RIDGE_WIDTH},{"STREAK",&STREAK},{"SPEC_GAIN",&SPEC_GAIN},{"SHININESS",&SHININESS},{"FLOW_CYCLE",&FLOW_CYCLE},{"V_MAX",&V_MAX},{"H_MAX",&H_MAX},{"MATURITY_EASE_SEC",&MF::MATURITY_EASE_SEC},
		{"MATURITY_TIME_WEIGHT",&MF::MATURITY_TIME_WEIGHT},{"HYPHA_SPAWN_PER_SEC",&MF::HYPHA_SPAWN_PER_SEC},
		{"HYPHA_SPEED_MM_S",&MF::HYPHA_SPEED_MM_S},{"TURN_NOISE_DEG",&MF::TURN_NOISE_DEG},{"ATTRACT_WEIGHT",&MF::ATTRACT_WEIGHT},
		{"ATTRACT_PLANT",&MF::ATTRACT_PLANT},{"BRANCH_PER_MM",&MF::BRANCH_PER_MM},{"TIP_LIFE_SEC",&MF::TIP_LIFE_SEC},
		{"HOME_MM",&MF::HOME_MM},{"MIN_MOBILITY",&MF::MIN_MOBILITY},{"TRACE_MEMORY_MIN",&MF::TRACE_MEMORY_MIN},
		{"HYPHA_WIDTH_PX",&MF::HYPHA_WIDTH_PX},{"TOUCH_MM",&MF::TOUCH_MM},
		{"PATCH_SCALE_MM",&PATCH_SCALE_MM},{"PATCH_FLOW",&PATCH_FLOW},{"PATCH_CONTRAST",&PATCH_CONTRAST},
		{"EDGE_COOL",&EDGE_COOL},{"RIM_GAIN",&RIM_GAIN},{"WARM_GAIN",&WARM_GAIN},{"GLOW_RADIUS_PX",&GLOW_RADIUS_PX},
		{"GLOW_STRENGTH",&GLOW_STRENGTH},{"HYPHA_BRIGHTNESS",&HYPHA_BRIGHTNESS},{"PATCH_BRIGHTNESS",&PATCH_BRIGHTNESS} };
	for (F & f : fs) if (k == f.n) { *f.p = v; printf("set %s = %g\n", f.n, v); return true; }
	if (k == "MAX_TIPS") { MF::MAX_TIPS = (int)v; return true; }
	if (k == "MAX_GENERATION") { MF::MAX_GENERATION = (int)v; return true; }
	if (k == "THREADS") { MF::THREADS = (int)v; return true; }
	fprintf(stderr, "unknown parameter %s\n", k.c_str());
	return false;
}

int main(int argc, char ** argv)
{
	if (argc < 2) { fprintf(stderr, "usage: %s <outdir> [repoRoot] [noise|ident|bench|story|quick ...] [KEY=value ...]\n", argv[0]); return 1; }
	OUT = argv[1];
	if (argc > 2) ROOT = argv[2];
	if (!readPreset(ROOT + "/bin/data/settings/ecologyParamsDesigner.xml", PRESET)) { fprintf(stderr, "no preset\n"); return 1; }
	setvbuf(stdout, nullptr, _IOLBF, 0);
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
