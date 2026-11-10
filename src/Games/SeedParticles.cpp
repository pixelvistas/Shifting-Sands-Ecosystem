#include "SeedParticles.h"
#include <algorithm>
#include <chrono>
#include <cmath>

bool SeedParticles::ENABLED = true;
float SeedParticles::WINDOW_SEC = 0.5f;
float SeedParticles::SEEDS_PER_UNIT_GROWTH = 20.0f;
int SeedParticles::MAX_PARTICLES = 20000;
float SeedParticles::GROWTH_EPS = 0.002f;
float SeedParticles::WIND_DIR_DEG = 30.0f;
float SeedParticles::WIND_MM_PER_SEC = 6.0f;
float SeedParticles::CURL_MM_PER_SEC = 5.0f;
float SeedParticles::CURL_SCALE_MM = 60.0f;
float SeedParticles::CURL_PERIOD_SEC = 20.0f;
float SeedParticles::OUTWARD_MM_PER_SEC = 4.0f;
// pioneer: light seed, fast and long-drifting; intermediate about half;
// climax: heavy seed, settles almost where it forms.
float SeedParticles::SPEED[3] = { 1.0f, 0.5f, 0.05f };
float SeedParticles::LIFE_SEC[3] = { 6.0f, 3.0f, 1.5f };
bool SeedParticles::MORTALITY_MOTES = false;
float SeedParticles::MOTES_PER_UNIT_LOSS = 20.0f;
float SeedParticles::MOTE_LIFE_SEC = 1.5f;
// pioneer #C9DB6E, intermediate #5E9B4C, climax #2F5E34 (as PlantLayer)
const float SeedParticles::ROLE_RGB[3][3] = {
	{ 0xC9 / 255.0f, 0xDB / 255.0f, 0x6E / 255.0f },
	{ 0x5E / 255.0f, 0x9B / 255.0f, 0x4C / 255.0f },
	{ 0x2F / 255.0f, 0x5E / 255.0f, 0x34 / 255.0f } };

static const float FADE_IN_SEC = 0.15f;
static const float TWO_PI = 6.28318530718f;

void SeedParticles::roleColour(int r, float soil, float out[3])
{
	const float * c = ROLE_RGB[std::max(0, std::min(2, r))];
	float s = std::min(1.0f, std::max(0.0f, soil));
	float grey = (0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2]) * 0.45f;
	for (int k = 0; k < 3; k++)
		out[k] = grey + (c[k] - grey) * s;
}

float SeedParticles::rnd()
{
	rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
	return (rng & 0xFFFFFF) / 16777216.0f;
}

void SeedParticles::reset()
{
	x.clear(); y.clear(); vx.clear(); vy.clear(); age.clear(); life.clear(); col.clear();
	role.clear(); kind.clear();
	haveSnap = false;
	windowElapsed = 0.0f;
	windowSteps = 0;
	closing = -1;
	for (int r = 0; r < 3; r++) { seedPool[r] = Pool(); motePool[r] = Pool(); nextSeed[r] = Pool(); nextMote[r] = Pool(); }
}

void SeedParticles::removeAt(size_t i)
{
	size_t last = x.size() - 1;
	if (i != last) {
		x[i] = x[last]; y[i] = y[last]; vx[i] = vx[last]; vy[i] = vy[last];
		age[i] = age[last]; life[i] = life[last]; role[i] = role[last]; kind[i] = kind[last];
		for (int k = 0; k < 3; k++) col[i * 3 + k] = col[last * 3 + k];
	}
	x.pop_back(); y.pop_back(); vx.pop_back(); vy.pop_back(); age.pop_back(); life.pop_back();
	role.pop_back(); kind.pop_back(); col.resize(col.size() - 3);
}

// Divergence-free field from a stream function psi (two sinusoid terms
// with drifting phases): v = (dpsi/dy, -dpsi/dx), magnitude ~1.
void SeedParticles::curl(float xmm, float ymm, float & cx, float & cy) const
{
	const float k = TWO_PI / std::max(1.0f, CURL_SCALE_MM);
	const float w = TWO_PI / std::max(0.1f, CURL_PERIOD_SEC);
	const float t = (float)time;
	const float a = k * xmm + w * t, b = 0.8f * k * ymm - 0.7f * w * t;
	const float c = 1.7f * k * (0.6f * xmm + 0.8f * ymm) + 1.3f * w * t;
	const float dpdx = std::cos(a) * std::cos(b) + 0.5f * 1.7f * 0.6f * std::cos(c);
	const float dpdy = -0.8f * std::sin(a) * std::sin(b) + 0.5f * 1.7f * 0.8f * std::cos(c);
	cx = dpdy;
	cy = -dpdx;
}

void SeedParticles::spawn(int cell, int r, int k, const float * soil, const float * const cover[3])
{
	const int gx = cell % cols, gy = cell / cols;
	float ux = 0.0f, uy = 0.0f;
	float c[3];
	roleColour(r, soil ? soil[cell] : 0.0f, c);
	float lifeSec;
	if (k == KIND_SEED) {
		// Away from the growth front: down this role's cover gradient.
		const float * v = cover[r];
		float gxd = v[gy * cols + std::min(cols - 1, gx + 1)] - v[gy * cols + std::max(0, gx - 1)];
		float gyd = v[std::min(rows - 1, gy + 1) * cols + gx] - v[std::max(0, gy - 1) * cols + gx];
		float len = std::sqrt(gxd * gxd + gyd * gyd);
		if (len > 1e-4f) { ux = -gxd / len; uy = -gyd / len; }
		else { float a = rnd() * TWO_PI; ux = std::cos(a); uy = std::sin(a); }
		float s = OUTWARD_MM_PER_SEC * SPEED[r];
		ux *= s; uy *= s;
		lifeSec = LIFE_SEC[r] * (0.75f + 0.5f * rnd());
	} else {
		// Mortality mote: desaturated (70 % towards grey), stays put.
		float grey = 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];
		for (int q = 0; q < 3; q++) c[q] = c[q] + (grey - c[q]) * 0.7f;
		lifeSec = MOTE_LIFE_SEC * (0.75f + 0.5f * rnd());
	}
	x.push_back(gx + rnd()); y.push_back(gy + rnd());
	vx.push_back(ux); vy.push_back(uy);
	age.push_back(0.0f); life.push_back(std::max(0.05f, lifeSec));
	role.push_back((uint8_t)r); kind.push_back((uint8_t)k);
	col.push_back(c[0]); col.push_back(c[1]); col.push_back(c[2]);
	spawned++;
}

void SeedParticles::curlAt(float gx, float gy, float & cx, float & cy) const
{
	float fx = gx / CURL_STEP, fy = gy / CURL_STEP;
	int i0 = std::max(0, std::min((int)fx, curlW - 2)), j0 = std::max(0, std::min((int)fy, curlH - 2));
	float tx = std::min(1.0f, std::max(0.0f, fx - i0)), ty = std::min(1.0f, std::max(0.0f, fy - j0));
	size_t a = (size_t)j0 * curlW + i0, b = a + 1, c = a + curlW, d = c + 1;
	cx = (curlX[a] * (1 - tx) + curlX[b] * tx) * (1 - ty) + (curlX[c] * (1 - tx) + curlX[d] * tx) * ty;
	cy = (curlY[a] * (1 - tx) + curlY[b] * tx) * (1 - ty) + (curlY[c] * (1 - tx) + curlY[d] * tx) * ty;
}

float SeedParticles::alpha(size_t i) const
{
	float a = std::min(1.0f, age[i] / FADE_IN_SEC) * (1.0f - age[i] / life[i]);
	return std::max(0.0f, std::min(1.0f, a));
}

float SeedParticles::sizeFactor(size_t i) const
{
	return kind[i] == KIND_MOTE ? std::max(0.0f, 1.0f - age[i] / life[i]) : 1.0f;
}

void SeedParticles::update(const float * const cover[3], const float * soil, const unsigned char * water,
	int c, int r, float mm, float dt, int interleave)
{
	auto t0 = std::chrono::steady_clock::now();
	if (c <= 0 || r <= 0 || mm <= 0.0f || !cover[0] || !cover[1] || !cover[2])
		return;
	if (c != cols || r != rows || mm != mmPerCell) {
		reset();
		cols = c; rows = r; mmPerCell = mm;
		for (int q = 0; q < 3; q++) { // no reallocation on the first window close
			for (Pool * p : { &seedPool[q], &nextSeed[q] }) { p->cell.reserve((size_t)c * r); p->cdf.reserve((size_t)c * r); }
		}
	}
	if (!ENABLED) {
		if (!x.empty() || haveSnap) reset();
		return;
	}
	dt = std::max(0.0f, std::min(dt, 0.1f));
	time += dt;
	const int n = cols * rows;

	// Growth window (see the header): close only after a multiple of the
	// model's interleave, so every row has the same number of updates.
	if (!haveSnap) {
		snap.resize((size_t)3 * n);
		for (int q = 0; q < 3; q++) std::copy(cover[q], cover[q] + n, snap.begin() + (size_t)q * n);
		haveSnap = true;
		windowElapsed = 0.0f;
		windowSteps = 0;
	}
	windowElapsed += dt;
	windowSteps++;
	const int K = std::max(1, interleave);
	if (closing < 0 && windowElapsed >= WINDOW_SEC && windowSteps % K == 0) {
		closing = 0;
		closeLen = windowElapsed;
		windowElapsed = 0.0f;
		windowSteps = 0;
		for (int q = 0; q < 3; q++) {
			nextSeed[q].cell.clear(); nextSeed[q].cdf.clear(); nextSeed[q].total = 0.0f;
			nextMote[q].cell.clear(); nextMote[q].cdf.clear(); nextMote[q].total = 0.0f;
		}
	}
	if (closing >= 0) {
		// Close row class `closing` this frame: rows closing, closing + K, ...
		// Its next close is a multiple of K frames later, so every row's
		// window holds the same number of model updates.
		for (int gy = closing; gy < rows; gy += K)
			for (int q = 0; q < 3; q++) {
				Pool & sp = nextSeed[q];
				Pool & mp = nextMote[q];
				const float * now = cover[q] + (size_t)gy * cols;
				float * old = &snap[(size_t)q * n + (size_t)gy * cols];
				const unsigned char * wr = water ? water + (size_t)gy * cols : nullptr;
				for (int gx = 0; gx < cols; gx++) {
					float d = now[gx] - old[gx];
					old[gx] = now[gx];
					if (wr && wr[gx]) continue;
					if (d > GROWTH_EPS) { sp.total += d; sp.cell.push_back(gy * cols + gx); sp.cdf.push_back(sp.total); }
					else if (MORTALITY_MOTES && -d > GROWTH_EPS) { mp.total -= d; mp.cell.push_back(gy * cols + gx); mp.cdf.push_back(mp.total); }
				}
			}
		if (++closing >= K) {
			// Spread this window's seeds over the next window.
			for (int q = 0; q < 3; q++) {
				std::swap(seedPool[q].cell, nextSeed[q].cell); std::swap(seedPool[q].cdf, nextSeed[q].cdf);
				seedPool[q].total = nextSeed[q].total;
				seedPool[q].rate = nextSeed[q].total * SEEDS_PER_UNIT_GROWTH / closeLen;
				std::swap(motePool[q].cell, nextMote[q].cell); std::swap(motePool[q].cdf, nextMote[q].cdf);
				motePool[q].total = nextMote[q].total;
				motePool[q].rate = nextMote[q].total * MOTES_PER_UNIT_LOSS / closeLen;
			}
			closing = -1;
		}
	}

	// Spawn.
	const size_t cap = (size_t)std::max(0, MAX_PARTICLES);
	for (int q = 0; q < 3; q++) {
		for (int pk = 0; pk < 2; pk++) {
			Pool & p = pk == 0 ? seedPool[q] : motePool[q];
			if (p.total <= 0.0f || p.cell.empty()) { p.acc = 0.0; continue; }
			p.acc += p.rate * dt;
			while (p.acc >= 1.0) {
				p.acc -= 1.0;
				if (x.size() >= cap) { p.acc = 0.0; break; }
				float u = rnd() * p.total;
				size_t j = (size_t)(std::upper_bound(p.cdf.begin(), p.cdf.end(), u) - p.cdf.begin());
				j = std::min(j, p.cell.size() - 1);
				spawn(p.cell[j], q, pk == 0 ? KIND_SEED : KIND_MOTE, soil, cover);
			}
		}
	}

	// Curl field for this frame on the coarse lattice.
	curlW = cols / CURL_STEP + 2;
	curlH = rows / CURL_STEP + 2;
	curlX.resize((size_t)curlW * curlH);
	curlY.resize((size_t)curlW * curlH);
	if (!x.empty())
		for (int j = 0; j < curlH; j++)
			for (int i2 = 0; i2 < curlW; i2++)
				curl(i2 * CURL_STEP * mmPerCell, j * CURL_STEP * mmPerCell, curlX[(size_t)j * curlW + i2], curlY[(size_t)j * curlW + i2]);

	// Move and age.
	const float wr = WIND_DIR_DEG * TWO_PI / 360.0f;
	const float wx = std::cos(wr) * WIND_MM_PER_SEC, wy = std::sin(wr) * WIND_MM_PER_SEC;
	const float inv = 1.0f / mmPerCell;
	size_t i = 0;
	while (i < x.size()) {
		age[i] += dt;
		if (age[i] >= life[i]) { removeAt(i); continue; }
		if (kind[i] == KIND_SEED) {
			float s = SPEED[role[i]];
			float cx = 0.0f, cy = 0.0f;
			if (s > 0.0f && CURL_MM_PER_SEC != 0.0f)
				curlAt(x[i], y[i], cx, cy);
			float mx = vx[i] + s * (wx + CURL_MM_PER_SEC * cx);
			float my = vy[i] + s * (wy + CURL_MM_PER_SEC * cy);
			x[i] += mx * dt * inv;
			y[i] += my * dt * inv;
			if (x[i] < 0.0f || y[i] < 0.0f || x[i] >= cols || y[i] >= rows) { removeAt(i); continue; }
			if (water && water[(int)y[i] * cols + (int)x[i]]) { removeAt(i); continue; } // seeds land on water: gone
		}
		i++;
	}
	lastMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}
