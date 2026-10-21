#include "DewitzFlow.h"
#include <algorithm>
#include <chrono>
#include <cmath>

// Defaults - see the header note and docs/DEVLOG.md (2026-10-21).
int DewitzFlow::DOWNSAMPLE = 3;
float DewitzFlow::TAU = 1.98f;
bool DewitzFlow::USE_MRT = true;
float DewitzFlow::INLET_SPEED = 0.08f;
int DewitzFlow::INLET_EDGE = DewitzFlow::EDGE_LEFT;
int DewitzFlow::STEPS_PER_FRAME = 1;
float DewitzFlow::OBSTACLE_HEIGHT_MM = 25.0f;
float DewitzFlow::OBSTACLE_SLOPE = 0.8f;
float DewitzFlow::OBSTACLE_SMOOTH_SEC = 0.3f;
float DewitzFlow::TERRAIN_SMOOTH_SEC = 0.3f;
// tools/dewitz_proto.py saturates the downhill term at ~1.17x its mounds'
// peak slope (2 vs ~1.7 per cell); its mounds at box scale peak at ~0.44
// rise/run, so the box equivalent is ~0.5.
float DewitzFlow::GRADIENT_SATURATION = 0.5f;
float DewitzFlow::W_FLOW = 1.0f;
float DewitzFlow::W_GRAD = 0.5f;
float DewitzFlow::W_ATTR = 0.5f;
// tools/dewitz_proto.py moves 1.2 cells/frame on a 240-cell-wide grid (~0.5% of
// the width per frame); on the 519-cell box grid at 40 fps that is ~100 cells/s.
float DewitzFlow::SPEED = 100.0f;
float DewitzFlow::ATTRACTOR_RADIUS = 20.0f;
int DewitzFlow::PARTICLE_COUNT = 20000;
// 600 frames at 40 fps, matching tools/dewitz_proto.py (age > 600 respawns).
float DewitzFlow::PARTICLE_LIFE_SEC = 15.0f;

// D2Q9, Lallemand & Luo (2000) ordering: rest, E, N, W, S, NE, NW, SW, SE.
// "N" is +y, which here is down the grid (row order); the physics is
// symmetric, only the labels are conventional.
static const int CX[9] = { 0, 1, 0, -1, 0, 1, -1, -1, 1 };
static const int CY[9] = { 0, 0, 1, 0, -1, 1, 1, -1, -1 };
static const float W[9] = { 4.0f / 9, 1.0f / 9, 1.0f / 9, 1.0f / 9, 1.0f / 9,
	1.0f / 36, 1.0f / 36, 1.0f / 36, 1.0f / 36 };
static const int OPP[9] = { 0, 3, 4, 1, 2, 7, 8, 5, 6 };

// MRT relaxation rates for the non-conserved moments other than the two
// shear moments (which use 1/TAU): energy, energy square, heat fluxes.
// Lallemand & Luo's (2000) linear-stability-optimised values.
static const float S_E = 1.64f, S_EPS = 1.54f, S_Q = 1.9f;

// Hysteresis thresholds on the obstacle evidence (0..1).
static const float EVIDENCE_CLOSE = 0.65f, EVIDENCE_OPEN = 0.35f;

static double nowMs()
{
	using namespace std::chrono;
	return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

void DewitzFlow::resize(int c, int r, int d)
{
	cols = c; rows = r; ds = d;
	nx = std::max(3, cols / ds);
	ny = std::max(3, rows / ds);
	size_t n = (size_t)nx * ny;
	f.assign(n * Q, 0.0f);
	fTmp.assign(n * Q, 0.0f);
	ux.assign(n, 0.0f);
	uy.assign(n, 0.0f);
	rho.assign(n, 1.0f);
	elev.assign(n, 0.0f);
	blurred.assign(n, 0.0f);
	downX.assign(n, 0.0f);
	downY.assign(n, 0.0f);
	evidence.assign(n, 0.0f);
	solid.assign(n, 0);
	for (size_t c2 = 0; c2 < n; c2++)
		setEquilibrium(&f[c2 * Q], 1.0f, 0.0f, 0.0f);
	terrainReady = false;
	px.clear(); py.clear(); age.clear();
}

void DewitzFlow::setEquilibrium(float * fc, float r, float u, float v) const
{
	float uu = u * u + v * v;
	for (int i = 0; i < Q; i++) {
		float cu = CX[i] * u + CY[i] * v;
		fc[i] = W[i] * r * (1.0f + 3.0f * cu + 4.5f * cu * cu - 1.5f * uu);
	}
}

void DewitzFlow::collideBGK(float * fc, float & r, float & u, float & v) const
{
	float omega = 1.0f / std::max(0.51f, TAU);
	float uu = u * u + v * v;
	for (int i = 0; i < Q; i++) {
		float cu = CX[i] * u + CY[i] * v;
		float feq = W[i] * r * (1.0f + 3.0f * cu + 4.5f * cu * cu - 1.5f * uu);
		fc[i] += omega * (feq - fc[i]);
	}
}

// Standard D2Q9 MRT (Lallemand & Luo 2000). Moments m = M f with
// m = (rho, e, eps, jx, qx, jy, qy, pxx, pxy); relax the non-conserved
// ones toward equilibrium, f -= M^-1 S (m - meq), using
// M^-1 = M^T diag(1/9, 1/36, 1/36, 1/6, 1/12, 1/6, 1/12, 1/4, 1/4).
void DewitzFlow::collideMRT(float * fc, float & r, float & u, float & v) const
{
	const float f0 = fc[0], f1 = fc[1], f2 = fc[2], f3 = fc[3], f4 = fc[4];
	const float f5 = fc[5], f6 = fc[6], f7 = fc[7], f8 = fc[8];
	float jx = r * u, jy = r * v;
	float j2 = (jx * jx + jy * jy) / r;

	float e = -4.0f * f0 - (f1 + f2 + f3 + f4) + 2.0f * (f5 + f6 + f7 + f8);
	float eps = 4.0f * f0 - 2.0f * (f1 + f2 + f3 + f4) + (f5 + f6 + f7 + f8);
	float qx = -2.0f * f1 + 2.0f * f3 + f5 - f6 - f7 + f8;
	float qy = -2.0f * f2 + 2.0f * f4 + f5 + f6 - f7 - f8;
	float pxx = f1 - f2 + f3 - f4;
	float pxy = f5 - f6 + f7 - f8;

	float sNu = 1.0f / std::max(0.51f, TAU);
	float c1 = S_E * (e - (-2.0f * r + 3.0f * j2)) / 36.0f;
	float c2 = S_EPS * (eps - (r - 3.0f * j2)) / 36.0f;
	float c4 = S_Q * (qx + jx) / 12.0f;
	float c6 = S_Q * (qy + jy) / 12.0f;
	float c7 = sNu * (pxx - (jx * jx - jy * jy) / r) / 4.0f;
	float c8 = sNu * (pxy - jx * jy / r) / 4.0f;

	fc[0] = f0 - (-4.0f * c1 + 4.0f * c2);
	fc[1] = f1 - (-c1 - 2.0f * c2 - 2.0f * c4 + c7);
	fc[2] = f2 - (-c1 - 2.0f * c2 - 2.0f * c6 - c7);
	fc[3] = f3 - (-c1 - 2.0f * c2 + 2.0f * c4 + c7);
	fc[4] = f4 - (-c1 - 2.0f * c2 + 2.0f * c6 - c7);
	fc[5] = f5 - (2.0f * c1 + c2 + c4 + c6 + c8);
	fc[6] = f6 - (2.0f * c1 + c2 - c4 + c6 - c8);
	fc[7] = f7 - (2.0f * c1 + c2 - c4 - c6 + c8);
	fc[8] = f8 - (2.0f * c1 + c2 + c4 - c6 - c8);
}

void DewitzFlow::update(const float * elevMM, int c, int r, float mmPerCell, float dt)
{
	updateTerrain(elevMM, c, r, mmPerCell, dt);
	if (nx <= 0)
		return;
	double t0 = nowMs();
	int n = std::max(1, std::min(8, STEPS_PER_FRAME));
	for (int s = 0; s < n; s++)
		stepLattice();
	latticeMs = (float)(nowMs() - t0);
	advectParticles(dt);
}

void DewitzFlow::updateTerrain(const float * elevMM, int c, int r, float mmPerCell, float dt)
{
	int d = std::max(1, DOWNSAMPLE);
	if (c < 3 * d || r < 3 * d)
		return;
	if (c != cols || r != rows || d != ds)
		resize(c, r, d);
	mmPerLatticeCell = std::max(0.01f, mmPerCell) * ds;

	// Downsample to the lattice (block mean of finite samples), then
	// smooth in time.
	float a = terrainReady ? 1.0f - std::exp(-dt / std::max(0.01f, TERRAIN_SMOOTH_SEC)) : 1.0f;
	for (int j = 0; j < ny; j++) {
		for (int i = 0; i < nx; i++) {
			float sum = 0.0f;
			int cnt = 0;
			for (int y = j * ds; y < (j + 1) * ds; y++)
				for (int x = i * ds; x < (i + 1) * ds; x++) {
					float e = elevMM[y * cols + x];
					if (std::isfinite(e)) { sum += e; cnt++; }
				}
			int k = j * nx + i;
			if (cnt > 0)
				elev[k] += (sum / cnt - elev[k]) * a;
		}
	}

	// 3x3 box blur, edges clamped.
	for (int j = 0; j < ny; j++) {
		for (int i = 0; i < nx; i++) {
			float sum = 0.0f;
			for (int dy = -1; dy <= 1; dy++)
				for (int dx = -1; dx <= 1; dx++) {
					int ii = std::min(nx - 1, std::max(0, i + dx));
					int jj = std::min(ny - 1, std::max(0, j + dy));
					sum += elev[jj * nx + ii];
				}
			blurred[j * nx + i] = sum / 9.0f;
		}
	}

	// Gradient (rise/run), capped downhill direction, raw obstacle test,
	// and obstacle evidence smoothed in time.
	float ea = terrainReady ? 1.0f - std::exp(-dt / std::max(0.01f, OBSTACLE_SMOOTH_SEC)) : 1.0f;
	float sat = std::max(1e-4f, GRADIENT_SATURATION);
	for (int j = 0; j < ny; j++) {
		for (int i = 0; i < nx; i++) {
			int k = j * nx + i;
			int il = std::max(0, i - 1), ir = std::min(nx - 1, i + 1);
			int jt = std::max(0, j - 1), jb = std::min(ny - 1, j + 1);
			float gx = (blurred[j * nx + ir] - blurred[j * nx + il]) / ((ir - il) * mmPerLatticeCell);
			float gy = (blurred[jb * nx + i] - blurred[jt * nx + i]) / ((jb - jt) * mmPerLatticeCell);
			float slope = std::sqrt(gx * gx + gy * gy);
			float scale = 1.0f / std::max(slope, sat);
			downX[k] = -gx * scale;
			downY[k] = -gy * scale;
			bool raw = blurred[k] > OBSTACLE_HEIGHT_MM || slope > OBSTACLE_SLOPE;
			evidence[k] += ((raw ? 1.0f : 0.0f) - evidence[k]) * ea;
		}
	}

	// Hysteresis on the evidence. Walls that open start at equilibrium
	// with their fluid neighbours' mean velocity.
	maskFlips = 0;
	obstacleCells = 0;
	for (int j = 0; j < ny; j++) {
		for (int i = 0; i < nx; i++) {
			int k = j * nx + i;
			if (!solid[k] && evidence[k] > EVIDENCE_CLOSE) {
				solid[k] = 1;
				ux[k] = uy[k] = 0.0f;
				maskFlips++;
			} else if (solid[k] && evidence[k] < EVIDENCE_OPEN) {
				float su = 0.0f, sv = 0.0f;
				int cnt = 0;
				for (int q = 1; q < Q; q++) {
					int ii = i + CX[q], jj = j + CY[q];
					if (ii < 0 || jj < 0 || ii >= nx || jj >= ny || solid[jj * nx + ii])
						continue;
					su += ux[jj * nx + ii];
					sv += uy[jj * nx + ii];
					cnt++;
				}
				if (cnt > 0) { su /= cnt; sv /= cnt; }
				solid[k] = 0;
				setEquilibrium(&f[(size_t)k * Q], 1.0f, su, sv);
				ux[k] = su; uy[k] = sv; rho[k] = 1.0f;
				maskFlips++;
			}
			obstacleCells += solid[k];
		}
	}
	terrainReady = true;
}

void DewitzFlow::stepLattice()
{
	if (nx <= 0)
		return;
	const int n = nx * ny;
	float * fp = f.data();
	float * tp = fTmp.data();

	// Collision.
	for (int k = 0; k < n; k++) {
		if (solid[k])
			continue;
		float * fc = fp + (size_t)k * Q;
		float r = 0.0f, mx = 0.0f, my = 0.0f;
		for (int i = 0; i < Q; i++) {
			r += fc[i];
			mx += fc[i] * CX[i];
			my += fc[i] * CY[i];
		}
		if (!std::isfinite(r) || !std::isfinite(mx) || !std::isfinite(my) || r < 0.2f || r > 5.0f) {
			if (!std::isfinite(r) || !std::isfinite(mx) || !std::isfinite(my))
				nonFinite++;
			setEquilibrium(fc, 1.0f, 0.0f, 0.0f);
			rho[k] = 1.0f; ux[k] = uy[k] = 0.0f;
			resets++;
			continue;
		}
		float u = mx / r, v = my / r;
		rho[k] = r; ux[k] = u; uy[k] = v;
		if (USE_MRT)
			collideMRT(fc, r, u, v);
		else
			collideBGK(fc, r, u, v);
	}

	// Streaming (pull) with halfway bounce-back off obstacles and off the
	// box edges; the inlet and outlet edges are overwritten below.
	for (int j = 0; j < ny; j++) {
		for (int i = 0; i < nx; i++) {
			int k = j * nx + i;
			if (solid[k])
				continue;
			float * tc = tp + (size_t)k * Q;
			const float * fc = fp + (size_t)k * Q;
			for (int q = 0; q < Q; q++) {
				int si = i - CX[q], sj = j - CY[q];
				if (si >= 0 && si < nx && sj >= 0 && sj < ny && !solid[sj * nx + si])
					tc[q] = fp[(size_t)(sj * nx + si) * Q + q];
				else
					tc[q] = fc[OPP[q]];
			}
		}
	}

	// Inlet: equilibrium, density 1, inward INLET_SPEED. Outlet (opposite
	// edge): pressure boundary, equilibrium at density 1 with the
	// interior neighbour's velocity.
	int inDx = INLET_EDGE == EDGE_LEFT ? 1 : INLET_EDGE == EDGE_RIGHT ? -1 : 0;
	int inDy = INLET_EDGE == EDGE_TOP ? 1 : INLET_EDGE == EDGE_BOTTOM ? -1 : 0;
	bool vertical = inDx != 0;
	int len = vertical ? ny : nx;
	for (int s = 0; s < len; s++) {
		int ii = vertical ? (inDx > 0 ? 0 : nx - 1) : s;
		int jj = vertical ? s : (inDy > 0 ? 0 : ny - 1);
		int k = jj * nx + ii;
		if (!solid[k])
			setEquilibrium(tp + (size_t)k * Q, 1.0f, INLET_SPEED * inDx, INLET_SPEED * inDy);

		int oi = vertical ? (inDx > 0 ? nx - 1 : 0) : s;
		int oj = vertical ? s : (inDy > 0 ? ny - 1 : 0);
		int ko = oj * nx + oi;
		int ki = (oj - inDy) * nx + (oi - inDx);
		if (solid[ko] || solid[ki])
			continue;
		const float * fi = tp + (size_t)ki * Q;
		float r = 0.0f, mx = 0.0f, my = 0.0f;
		for (int q = 0; q < Q; q++) {
			r += fi[q];
			mx += fi[q] * CX[q];
			my += fi[q] * CY[q];
		}
		if (r > 0.0f && std::isfinite(r))
			setEquilibrium(tp + (size_t)ko * Q, 1.0f, mx / r, my / r);
	}

	f.swap(fTmp);
	steps++;

	maxSpeed = 0.0f;
	for (int k = 0; k < n; k++) {
		if (solid[k])
			continue;
		float s2 = ux[k] * ux[k] + uy[k] * uy[k];
		if (s2 > maxSpeed)
			maxSpeed = s2;
	}
	maxSpeed = std::sqrt(maxSpeed);
}

float DewitzFlow::bilerp(const std::vector<float> & m, float lx, float ly) const
{
	float fx = std::min(std::max(lx, 0.0f), (float)(nx - 1));
	float fy = std::min(std::max(ly, 0.0f), (float)(ny - 1));
	int x0 = (int)fx, y0 = (int)fy;
	int x1 = std::min(x0 + 1, nx - 1), y1 = std::min(y0 + 1, ny - 1);
	float tx = fx - x0, ty = fy - y0;
	auto at = [&](int x, int y) { int k = y * nx + x; return solid[k] ? 0.0f : m[k]; };
	return (at(x0, y0) * (1 - tx) + at(x1, y0) * tx) * (1 - ty)
		+ (at(x0, y1) * (1 - tx) + at(x1, y1) * tx) * ty;
}

bool DewitzFlow::solidAtFine(float x, float y) const
{
	int i = (int)(x / ds), j = (int)(y / ds);
	if (i < 0 || j < 0 || i >= nx || j >= ny)
		return true;
	return solid[j * nx + i] != 0;
}

void DewitzFlow::respawn(int k, bool anywhere)
{
	std::uniform_real_distribution<float> U(0.0f, 1.0f);
	float w = (float)(nx * ds), h = (float)(ny * ds);
	float x = 0.0f, y = 0.0f;
	for (int attempt = 0; attempt < 8; attempt++) {
		if (anywhere) {
			x = U(rng) * w; y = U(rng) * h;
		} else {
			float along = U(rng), in = U(rng) * ds; // within the first lattice cell
			switch (INLET_EDGE) {
			case EDGE_LEFT:  x = in;         y = along * h; break;
			case EDGE_RIGHT: x = w - 1 - in; y = along * h; break;
			case EDGE_TOP:   x = along * w;  y = in;        break;
			default:         x = along * w;  y = h - 1 - in; break;
			}
		}
		if (!solidAtFine(x, y))
			break;
	}
	px[k] = x; py[k] = y;
	age[k] = 0.0f;
}

void DewitzFlow::advectParticles(float dt)
{
	if (nx <= 0)
		return;
	double t0 = nowMs();
	int target = std::max(0, PARTICLE_COUNT);
	if ((int)px.size() != target) {
		int old = (int)px.size();
		px.resize(target); py.resize(target); age.resize(target);
		for (int k = old; k < target; k++)
			respawn(k, true);
	}

	float inv = maxSpeed > 1e-6f ? 1.0f / maxSpeed : 0.0f;
	float w = (float)(nx * ds), h = (float)(ny * ds);
	float r2 = ATTRACTOR_RADIUS * ATTRACTOR_RADIUS;
	respawnsLastFrame = 0;
	exitsLastFrame = 0;
	for (int k = 0; k < (int)px.size(); k++) {
		float x = px[k], y = py[k];
		float lx = x / ds - 0.5f, ly = y / ds - 0.5f;

		float dirX = W_FLOW * bilerp(ux, lx, ly) * inv + W_GRAD * bilerp(downX, lx, ly);
		float dirY = W_FLOW * bilerp(uy, lx, ly) * inv + W_GRAD * bilerp(downY, lx, ly);

		if (W_ATTR != 0.0f) {
			float ax = 0.0f, ay = 0.0f;
			for (const Attractor & at : attractors) {
				float dx = at.x - x, dy = at.y - y;
				float d2 = dx * dx + dy * dy;
				if (d2 < 1e-6f)
					continue;
				float d = std::sqrt(d2);
				float pull = std::min(1.0f, r2 / d2); // gravity-like 1/r^2, capped
				ax += dx / d * pull;
				ay += dy / d * pull;
			}
			dirX += W_ATTR * ax;
			dirY += W_ATTR * ay;
		}

		x += SPEED * dirX * dt;
		y += SPEED * dirY * dt;
		age[k] += dt;
		bool outlet = (INLET_EDGE == EDGE_LEFT && x >= w) || (INLET_EDGE == EDGE_RIGHT && x < 0)
			|| (INLET_EDGE == EDGE_TOP && y >= h) || (INLET_EDGE == EDGE_BOTTOM && y < 0);
		if (outlet)
			exitsLastFrame++;
		if (x < 0 || y < 0 || x >= w || y >= h || solidAtFine(x, y)
			|| (PARTICLE_LIFE_SEC > 0.0f && age[k] > PARTICLE_LIFE_SEC)) {
			respawn(k, false);
			respawnsLastFrame++;
			continue;
		}
		px[k] = x; py[k] = y;
	}
	particleMs = (float)(nowMs() - t0);
}

void DewitzFlow::sampleTerms(float x, float y, float & flowX, float & flowY, float & downhillX, float & downhillY) const
{
	flowX = flowY = downhillX = downhillY = 0.0f;
	if (nx <= 0)
		return;
	float lx = x / ds - 0.5f, ly = y / ds - 0.5f;
	float inv = maxSpeed > 1e-6f ? 1.0f / maxSpeed : 0.0f;
	flowX = bilerp(ux, lx, ly) * inv;
	flowY = bilerp(uy, lx, ly) * inv;
	downhillX = bilerp(downX, lx, ly);
	downhillY = bilerp(downY, lx, ly);
}

float DewitzFlow::speedAt(int i, int j) const
{
	int k = j * nx + i;
	return solid[k] ? 0.0f : std::sqrt(ux[k] * ux[k] + uy[k] * uy[k]);
}

float DewitzFlow::densityAt(int i, int j) const
{
	return rho[j * nx + i];
}
