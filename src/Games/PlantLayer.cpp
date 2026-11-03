#include "PlantLayer.h"
#include <algorithm>
#include <cmath>

bool PlantLayer::DRAW_PLANTS = true;
float PlantLayer::PLANT_SPACING_MM = 15.0f;
float PlantLayer::PLANT_MIN_COVER = 0.15f;
float PlantLayer::PLANT_EASE_SEC = 2.0f;
float PlantLayer::MAX_RADIUS_MM[3] = { 3.0f, 6.0f, 10.0f };
// pioneer #C9DB6E, intermediate #5E9B4C, climax #2F5E34
const unsigned char PlantLayer::FILL[3][3] = { { 0xC9, 0xDB, 0x6E }, { 0x5E, 0x9B, 0x4C }, { 0x2F, 0x5E, 0x34 } };
// the same hues at 60% of their HSL lightness: #8B9E27, #385D2E, #1C381F
const unsigned char PlantLayer::RIM[3][3] = { { 0x8B, 0x9E, 0x27 }, { 0x38, 0x5D, 0x2E }, { 0x1C, 0x38, 0x1F } };

// Below this radius (mm) a shrinking plant counts as gone.
static const float GONE_MM = 0.05f;

void PlantLayer::build(int c, int r, float mm)
{
	cols = c; rows = r; mmPerCell = mm;
	spacing = std::max(1.0f, PLANT_SPACING_MM);
	nx = std::max(1, (int)std::ceil(cols * mmPerCell / spacing));
	ny = std::max(1, (int)std::ceil(rows * mmPerCell / spacing));
	const int n = nx * ny;
	jx.resize(n); jy.resize(n);
	uint32_t s = 2463534242u; // fixed seed: the same sites every time
	auto rnd = [&s]() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s & 0xFFFFFF) / 16777216.0f; };
	for (int i = 0; i < n; i++) {
		// +-0.35 spacing, quantised to the 1/255 steps the state texture holds.
		jx[i] = std::round((rnd() * 0.7f + 0.15f) * 255.0f) / 255.0f - 0.5f;
		jy[i] = std::round((rnd() * 0.7f + 0.15f) * 255.0f) / 255.0f - 0.5f;
	}
	role.assign(n, ROLE_NONE);
	radius.assign(n, 0.0f);
}

void PlantLayer::update(const float * const cover[3], const unsigned char * water, int c, int r, float mm, float dt)
{
	if (c <= 0 || r <= 0 || mm <= 0.0f)
		return;
	if (c != cols || r != rows || mm != mmPerCell || std::max(1.0f, PLANT_SPACING_MM) != spacing)
		build(c, r, mm);
	const float a = 1.0f - std::exp(-std::max(0.0f, dt) / std::max(0.01f, PLANT_EASE_SEC));
	for (int sy = 0; sy < ny; sy++)
		for (int sx = 0; sx < nx; sx++) {
			const int i = sy * nx + sx;
			// The model cell under the site centre.
			float xmm = (sx + 0.5f + jx[i]) * spacing, ymm = (sy + 0.5f + jy[i]) * spacing;
			int gx = (int)(xmm / mmPerCell), gy = (int)(ymm / mmPerCell);
			int targetRole = ROLE_NONE;
			float targetRadius = 0.0f;
			if (gx >= 0 && gx < cols && gy >= 0 && gy < rows) {
				const int k = gy * cols + gx;
				if (!(water && water[k])) {
					float p = cover[0][k], m = cover[1][k], cl = cover[2][k];
					int d = (p >= m && p >= cl) ? PIONEER : (m >= cl ? INTERMEDIATE : CLIMAX);
					float cv = d == PIONEER ? p : (d == INTERMEDIATE ? m : cl);
					if (cv > PLANT_MIN_COVER) {
						targetRole = d;
						// Capped at 1.1 x spacing: the shader only searches the 3x3 nearest sites.
						targetRadius = std::min(MAX_RADIUS_MM[d], 1.1f * spacing) * std::min(1.0f, cv);
					}
				}
			}
			// A different plant shrinks away first; then the new one grows in.
			if (role[i] != targetRole && role[i] != ROLE_NONE) {
				radius[i] += (0.0f - radius[i]) * a;
				if (radius[i] < GONE_MM) { radius[i] = 0.0f; role[i] = ROLE_NONE; }
				continue;
			}
			role[i] = targetRole;
			if (targetRole == ROLE_NONE) { radius[i] = 0.0f; continue; }
			radius[i] += (targetRadius - radius[i]) * a;
		}
}

float PlantLayer::radiusScale() const
{
	return std::max(0.1f, std::max(MAX_RADIUS_MM[0], std::max(MAX_RADIUS_MM[1], MAX_RADIUS_MM[2])));
}

void PlantLayer::packState(std::vector<unsigned char> & rgba) const
{
	rgba.resize((size_t)nx * ny * 4);
	const float inv = 1.0f / radiusScale();
	for (int i = 0; i < nx * ny; i++) {
		bool on = role[i] != ROLE_NONE && radius[i] >= GONE_MM;
		rgba[i * 4 + 0] = (unsigned char)(on ? role[i] + 1 : 0);
		rgba[i * 4 + 1] = (unsigned char)std::lround(std::min(1.0f, radius[i] * inv) * 255.0f);
		rgba[i * 4 + 2] = (unsigned char)std::lround((jx[i] + 0.5f) * 255.0f);
		rgba[i * 4 + 3] = (unsigned char)std::lround((jy[i] + 0.5f) * 255.0f);
	}
}

static inline float smoothstepf(float e0, float e1, float x)
{
	float t = std::min(1.0f, std::max(0.0f, (x - e0) / (e1 - e0)));
	return t * t * (3.0f - 2.0f * t);
}

// Must match heightMapShader.frag's plant drawing: radius and jitter come
// from the packed (8-bit) state, a 0.5 mm soft edge, rim width
// max(0.6 mm, 0.25 r).
void PlantLayer::renderOver(std::vector<unsigned char> & rgb, int w, int h, float pxPerMM, const unsigned char * waterAtPx) const
{
	std::vector<unsigned char> st;
	packState(st);
	const float scale = radiusScale();
	for (int i = 0; i < nx * ny; i++) {
		int rl = (int)st[i * 4] - 1;
		if (rl < 0) continue;
		float r = st[i * 4 + 1] / 255.0f * scale;
		int sx = i % nx, sy = i / nx;
		float cx = (sx + st[i * 4 + 2] / 255.0f) * spacing, cy = (sy + st[i * 4 + 3] / 255.0f) * spacing; // centre, mm
		float rimW = std::max(0.6f, 0.25f * r);
		int x0 = std::max(0, (int)((cx - r - 1) * pxPerMM)), x1 = std::min(w - 1, (int)((cx + r + 1) * pxPerMM));
		int y0 = std::max(0, (int)((cy - r - 1) * pxPerMM)), y1 = std::min(h - 1, (int)((cy + r + 1) * pxPerMM));
		for (int py = y0; py <= y1; py++)
			for (int px = x0; px <= x1; px++) {
				if (waterAtPx && waterAtPx[py * w + px]) continue;
				float d = std::hypot((px + 0.5f) / pxPerMM - cx, (py + 0.5f) / pxPerMM - cy);
				float cover = 1.0f - smoothstepf(r - 0.25f, r + 0.25f, d);
				if (cover <= 0.0f) continue;
				float rim = smoothstepf(r - rimW - 0.25f, r - rimW + 0.25f, d);
				unsigned char * p = &rgb[((size_t)py * w + px) * 3];
				for (int c = 0; c < 3; c++) {
					float col = FILL[rl][c] * (1.0f - rim) + RIM[rl][c] * rim;
					p[c] = (unsigned char)std::lround(p[c] * (1.0f - cover) + col * cover);
				}
			}
	}
}
