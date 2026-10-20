#include "LatticeFlow.h"
#include "VegetationField.h"
#include <algorithm>
#include <cmath>

// First guesses, untested on hardware - see the header note.
bool LatticeFlow::ENABLE = true;
int LatticeFlow::DOWNSAMPLE = 4;
float LatticeFlow::TAU = 0.55f;
float LatticeFlow::INLET_SPEED = 0.12f;
int LatticeFlow::STEPS_PER_FRAME = 2;
float LatticeFlow::WET_FRACTION = 0.5f;

// D2Q9 lattice: rest, 4 axis neighbours, 4 diagonals. Grid y points down,
// matching VegetationField's row order.
static const int CX[9] = { 0, 1, 0, -1, 0, 1, -1, -1, 1 };
static const int CY[9] = { 0, 0, 1, 0, -1, 1, 1, -1, -1 };
static const float W[9] = { 4.0f / 9, 1.0f / 9, 1.0f / 9, 1.0f / 9, 1.0f / 9,
	1.0f / 36, 1.0f / 36, 1.0f / 36, 1.0f / 36 };
static const int OPP[9] = { 0, 3, 4, 1, 2, 7, 8, 5, 6 };

void LatticeFlow::resize(int newNx, int newNy)
{
	nx = newNx;
	ny = newNy;
	size_t n = (size_t)nx * ny;
	f.assign(n * Q, 0.0f);
	fTmp.assign(n * Q, 0.0f);
	ux.assign(n, 0.0f);
	uy.assign(n, 0.0f);
	fluid.assign(n, 0);
	wasFluid.assign(n, 0);
	for (size_t c = 0; c < n; c++)
		setEquilibrium((int)c, 1.0f, 0.0f, 0.0f, f.data());
}

void LatticeFlow::setEquilibrium(int cell, float rho, float u, float v, float * dst)
{
	float uu = u * u + v * v;
	float * fc = dst + (size_t)cell * Q;
	for (int i = 0; i < Q; i++) {
		float cu = CX[i] * u + CY[i] * v;
		fc[i] = W[i] * rho * (1.0f + 3.0f * cu + 4.5f * cu * cu - 1.5f * uu);
	}
}

float LatticeFlow::cellDensity(const float * src, int cell) const
{
	const float * fc = src + (size_t)cell * Q;
	float rho = 0.0f;
	for (int i = 0; i < Q; i++)
		rho += fc[i];
	return rho;
}

void LatticeFlow::cellMoments(const float * src, int cell, float & rho, float & u, float & v) const
{
	const float * fc = src + (size_t)cell * Q;
	float mx = 0.0f, my = 0.0f;
	rho = 0.0f;
	for (int i = 0; i < Q; i++) {
		rho += fc[i];
		mx += fc[i] * CX[i];
		my += fc[i] * CY[i];
	}
	u = rho > 0.0f ? mx / rho : 0.0f;
	v = rho > 0.0f ? my / rho : 0.0f;
}

void LatticeFlow::update(const VegetationField & vf, float frameDt)
{
	if (!ENABLE)
		return;
	uint64_t t0 = ofGetElapsedTimeMicros();

	int cols = vf.getCols(), rows = vf.getRows();
	int newDs = std::max(1, DOWNSAMPLE);
	int newNx = cols / newDs, newNy = rows / newDs;
	if (newNx < 3 || newNy < 3)
		return;
	if (newNx != nx || newNy != ny || newDs != ds) {
		ds = newDs;
		resize(newNx, newNy);
	}
	const int n = nx * ny;

	// Fluid mask from the virtual-pipe model's (hysteresis-stabilised)
	// water classification. Cells that just became fluid start at rest.
	int needed = std::max(1, (int)std::ceil(WET_FRACTION * ds * ds));
	fluidCells = 0;
	for (int j = 0; j < ny; j++) {
		for (int i = 0; i < nx; i++) {
			int wet = 0;
			for (int y = j * ds; y < (j + 1) * ds; y++)
				for (int x = i * ds; x < (i + 1) * ds; x++)
					wet += vf.isWaterCell(x, y) ? 1 : 0;
			int c = j * nx + i;
			fluid[c] = wet >= needed ? 1 : 0;
			if (fluid[c] && !wasFluid[c])
				setEquilibrium(c, 1.0f, 0.0f, 0.0f, f.data());
			if (!fluid[c])
				ux[c] = uy[c] = 0.0f;
			wasFluid[c] = fluid[c];
			fluidCells += fluid[c];
		}
	}

	// Inlet/outlet geometry from VegetationField's source/sink settings, so
	// the lattice is driven from the same edge the pipe model fills from.
	int srcEdge = VegetationField::SOURCE_EDGE, sinkEdge = VegetationField::SINK_EDGE;
	auto inward = [](int edge, int & dx, int & dy) {
		dx = edge == VegetationField::EDGE_LEFT ? 1 : edge == VegetationField::EDGE_RIGHT ? -1 : 0;
		dy = edge == VegetationField::EDGE_TOP ? 1 : edge == VegetationField::EDGE_BOTTOM ? -1 : 0;
	};
	auto edgeCell = [&](int edge, int k) {
		switch (edge) {
		case VegetationField::EDGE_LEFT:  return k * nx;
		case VegetationField::EDGE_RIGHT: return k * nx + (nx - 1);
		case VegetationField::EDGE_TOP:   return k;
		default:                          return (ny - 1) * nx + k;
		}
	};
	int inDx, inDy, outDx, outDy;
	inward(srcEdge, inDx, inDy);
	inward(sinkEdge, outDx, outDy);
	int srcLen = (srcEdge == VegetationField::EDGE_LEFT || srcEdge == VegetationField::EDGE_RIGHT) ? ny : nx;
	int sinkLen = (sinkEdge == VegetationField::EDGE_LEFT || sinkEdge == VegetationField::EDGE_RIGHT) ? ny : nx;
	int span = std::max(1, (int)(srcLen * std::min(1.0f, VegetationField::SOURCE_SPAN)));
	int spanFirst = (srcLen - span) / 2;
	float uIn = std::min(INLET_SPEED, 0.2f);

	const float tau = std::max(0.51f, TAU);
	const float omega = 1.0f / tau;
	const int steps = std::max(1, std::min(8, STEPS_PER_FRAME));
	float * fp = f.data();
	float * tp = fTmp.data();
	maxLatticeSpeed = 0.0f;

	for (int s = 0; s < steps; s++) {
		// Collision (BGK) on fluid cells.
		for (int c = 0; c < n; c++) {
			if (!fluid[c])
				continue;
			float * fc = fp + (size_t)c * Q;
			float rho = 0.0f, mx = 0.0f, my = 0.0f;
			for (int i = 0; i < Q; i++) {
				rho += fc[i];
				mx += fc[i] * CX[i];
				my += fc[i] * CY[i];
			}
			if (!std::isfinite(rho) || rho < 0.2f || rho > 5.0f) {
				setEquilibrium(c, 1.0f, 0.0f, 0.0f, fp);
				ux[c] = uy[c] = 0.0f;
				resets++;
				continue;
			}
			float u = mx / rho, v = my / rho;
			ux[c] = u;
			uy[c] = v;
			float uu = u * u + v * v;
			for (int i = 0; i < Q; i++) {
				float cu = CX[i] * u + CY[i] * v;
				float feq = W[i] * rho * (1.0f + 3.0f * cu + 4.5f * cu * cu - 1.5f * uu);
				fc[i] += omega * (feq - fc[i]);
			}
		}

		// Streaming (pull) with halfway bounce-back off walls and the
		// grid boundary.
		for (int j = 0; j < ny; j++) {
			for (int i = 0; i < nx; i++) {
				int c = j * nx + i;
				if (!fluid[c])
					continue;
				float * tc = tp + (size_t)c * Q;
				const float * fc = fp + (size_t)c * Q;
				for (int q = 0; q < Q; q++) {
					int si = i - CX[q], sj = j - CY[q];
					if (si >= 0 && si < nx && sj >= 0 && sj < ny && fluid[sj * nx + si])
						tc[q] = fp[(size_t)(sj * nx + si) * Q + q];
					else
						tc[q] = fc[OPP[q]];
				}
			}
		}

		// Inlet: fluid cells on the source edge, within the source span,
		// are set to equilibrium at density 1 and the inward velocity.
		// (Taking density from the interior neighbour instead was tried on
		// a test channel and fed back: interior density rose, so inflow
		// rose, until lattice speeds passed 0.6 and cells went unstable.)
		inletCells = 0;
		for (int k = spanFirst; k < spanFirst + span; k++) {
			int c = edgeCell(srcEdge, k);
			if (!fluid[c])
				continue;
			setEquilibrium(c, 1.0f, uIn * inDx, uIn * inDy, tp);
			inletCells++;
		}
		// Outlet (pressure boundary): fluid cells on the sink edge are set
		// to equilibrium at the reference density 1 with their interior
		// neighbour's velocity, so fluid leaves freely and pressure cannot
		// build up inside and choke the inflow (found on a test channel:
		// a plain copy-from-neighbour outlet let the flow die away).
		for (int k = 0; k < sinkLen; k++) {
			int c = edgeCell(sinkEdge, k);
			int ci = c + outDy * nx + outDx;
			if (!fluid[c] || !fluid[ci])
				continue;
			float rho, u, v;
			cellMoments(tp, ci, rho, u, v);
			setEquilibrium(c, 1.0f, u, v, tp);
		}

		std::swap(fp, tp);
	}
	// Keep f holding the latest state regardless of how many swaps ran.
	if (fp != f.data())
		f.swap(fTmp);

	for (int c = 0; c < n; c++) {
		if (!fluid[c])
			continue;
		float sp = std::sqrt(ux[c] * ux[c] + uy[c] * uy[c]);
		if (sp > maxLatticeSpeed)
			maxLatticeSpeed = sp;
	}

	// One lattice cell = ds grid cells; one lattice step = frameDt/steps.
	velocityScale = frameDt > 0.0f ? ds * steps / frameDt : 0.0f;
	lastMs = (ofGetElapsedTimeMicros() - t0) / 1000.0f;
}

ofVec2f LatticeFlow::velocityAt(float gx, float gy) const
{
	if (!isReady() || !ENABLE)
		return ofVec2f(0.0f, 0.0f);
	// Lattice cell i covers grid cells [i*ds, (i+1)*ds); centre at (i+0.5)*ds.
	float fx = ofClamp(gx / ds - 0.5f, 0.0f, (float)(nx - 1));
	float fy = ofClamp(gy / ds - 0.5f, 0.0f, (float)(ny - 1));
	int x0 = (int)fx, y0 = (int)fy;
	int x1 = std::min(x0 + 1, nx - 1), y1 = std::min(y0 + 1, ny - 1);
	float tx = fx - x0, ty = fy - y0;
	auto at = [&](const std::vector<float> & m, int x, int y) {
		int c = y * nx + x;
		return fluid[c] ? m[c] : 0.0f;
	};
	auto bilerp = [&](const std::vector<float> & m) {
		return (at(m, x0, y0) * (1 - tx) + at(m, x1, y0) * tx) * (1 - ty)
			+ (at(m, x0, y1) * (1 - tx) + at(m, x1, y1) * tx) * ty;
	};
	return ofVec2f(bilerp(ux), bilerp(uy)) * velocityScale;
}
