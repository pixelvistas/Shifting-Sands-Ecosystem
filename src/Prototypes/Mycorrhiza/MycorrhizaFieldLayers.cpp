/***********************************************************************
MycorrhizaFieldLayers.cpp - MycorrhizaField, style "Patches and hyphae"
(the default since 2026-11-13; brief "Updated acceptance criteria (P2)").

Patches (the ground a hand has shaped):
- Each cell keeps a fast and a slow running mean of its own elevation
  (TOUCH_FAST_SEC, TOUCH_SLOW_SEC). Inside touched blocks, a cell is
  marked where they differ by more than PATCH_CELL_MM. Cells with no depth
  sample (a hand) keep their means and are not marked until the sand under
  them is seen again. The mark is closed (3 x 3 dilate then erode, so small
  gaps fill) and blurred (3 x 3) into a footprint with smooth edges.
- (2026-11-14) The footprint is the source of a light-liquid: a viscous
  shallow flow at half resolution (updateLiquid(), described there) that
  runs downhill and pools in basins, grows with continued interaction,
  ignores simulated water and evaporates over the trace memory. Presence
  is smoothstep(0, LIQUID_PRESENCE_MM, depth).
- Maturity (0..1, the colour) is a tracer carried with the liquid: a new
  mark resets it to 0; it eases up (MATURITY_EASE_SEC) toward the ground's
  life = max(total cover, soil S) from the succession model, never
  decreasing until the next mark. With MATURITY_TIME_WEIGHT = 0 (default)
  the colour is purely the model's recovery; above 0 it is blended with
  time since the mark (1 after MATURE_SEC).

Hyphae (threads between patches), tip growth:
- Tips are born on patch boundary cells (presence >= 0.5 next to < 0.5,
  not water), HYPHA_SPAWN_PER_SEC per mm of boundary x presence x
  (1 + HYPHA_TOUCH_BOOST x touch), so the network grows faster while people
  interact, heading
  outward along the presence gradient +-25 degrees, up to MAX_TIPS.
- Each step a tip advances HYPHA_SPEED_MM_S x mix(MIN_MOBILITY, 1,
  max(S, M)) x dt. Its heading is a persistent random walk (TURN_NOISE_DEG
  per sqrt(mm)) plus, after HOME_MM of growth, a pull up the gradient of an
  attractant: blurred patch presence plus ATTRACT_PLANT x plant cover, at
  touch-block resolution (ATTRACT_WEIGHT per mm). It steers away from water
  (probe 4 mm ahead, +-35 degrees), stops if both sides are water, and
  stops at the grid edge.
- Branching: BRANCH_PER_MM per mm grown, up to MAX_GENERATION; the child
  turns 30..60 degrees to either side, one generation deeper; width falls from HYPHA_WIDTH_PX to
  HYPHA_MIN_WIDTH_PX with generation.
- Fusing: a coarse occupancy grid (2 x 2 cells) remembers the thread of the
  last segment through each cell. After 6 mm of its own growth, a tip that
  enters a cell another thread (not its parent) passed more than 1 s
  earlier stops there, forming a loop. A tip that has left its home patch and enters a different patch
  stops there (a connection); re-entering its home patch turns it back.
- Stops: water, grid edge, TIP_LIFE_SEC, fusing, connecting, or not having
  left its home patch within 5 mm.
- Segments (x0, y0, x1, y1, width, birth) are stored every SEGMENT_MM,
  oldest dropped beyond MAX_SEGMENTS, fade with the trace memory and are
  removed when either end is under water.

Everything here only reads the inputs; nothing is written back to the
succession model.
***********************************************************************/

#include "MycorrhizaField.h"
#include <algorithm>
#include <cmath>

float MycorrhizaField::PATCH_CELL_MM = 3.0f;
float MycorrhizaField::MATURITY_EASE_SEC = 3.0f;
float MycorrhizaField::MATURITY_TIME_WEIGHT = 0.0f;
float MycorrhizaField::MATURE_SEC = 30.0f;
float MycorrhizaField::LIQUID_SOURCE_MM_S = 6.0f;
float MycorrhizaField::LIQUID_VISCOSITY = 1.3f;
float MycorrhizaField::LIQUID_PRESENCE_MM = 1.5f;
int MycorrhizaField::LIQUID_SUBSTEPS = 2;
float MycorrhizaField::HYPHA_TOUCH_BOOST = 3.0f;
float MycorrhizaField::HYPHA_SPAWN_PER_SEC = 0.006f;
int MycorrhizaField::MAX_TIPS = 2000;
int MycorrhizaField::MAX_SEGMENTS = 60000;
float MycorrhizaField::HYPHA_SPEED_MM_S = 10.0f;
float MycorrhizaField::TURN_NOISE_DEG = 12.0f;
float MycorrhizaField::ATTRACT_WEIGHT = 0.08f;
float MycorrhizaField::ATTRACT_PLANT = 0.3f;
float MycorrhizaField::BRANCH_PER_MM = 0.02f;
int MycorrhizaField::MAX_GENERATION = 4;
float MycorrhizaField::HYPHA_WIDTH_PX = 2.2f;
float MycorrhizaField::HYPHA_MIN_WIDTH_PX = 0.8f;
float MycorrhizaField::TIP_LIFE_SEC = 20.0f;
float MycorrhizaField::SEGMENT_MM = 2.0f;
float MycorrhizaField::HOME_MM = 15.0f;

static const float L_DEG = 3.14159265358979f / 180.0f;
static const float LIQUID_CUTOFF_MM = 0.1f;   // thinner liquid is dropped (invisible; keeps the wet area small)
static const float FUSE_AFTER_MM = 6.0f;
static const float FUSE_MIN_AGE_SEC = 1.0f;
static const float LEAVE_HOME_MM = 5.0f;
static const float PROBE_MM = 4.0f;
static const float SPAWN_JITTER_DEG = 25.0f;
static const int ATTRACT_BLUR_BLOCKS = 3;     // box radius, applied twice
static const float GONE_BRIGHTNESS = 0.02f;   // segments dimmer than this are dropped

float MycorrhizaField::fadeAt(float birth) const
{
	const double mem = std::max(0.05f, TRACE_MEMORY_MIN) * 60.0;
	return (float)std::pow(0.1, std::max(0.0, time - birth) / mem);
}

void MycorrhizaField::clearLayers()
{
	std::fill(presenceMap.begin(), presenceMap.end(), 0.0f);
	std::fill(maturityMap.begin(), maturityMap.end(), 0.0f);
	std::fill(markTime.begin(), markTime.end(), 0.0f);
	std::fill(footprint.begin(), footprint.end(), 0.0f);
	for (std::vector<float> * v : { &ldepth, &lq, &lmat, &lvx, &lvy, &lmark }) std::fill(v->begin(), v->end(), 0.0f);
	std::fill(mark.begin(), mark.end(), (unsigned char)0);
	std::fill(attractBlocks.begin(), attractBlocks.end(), 0.0f);
	std::fill(warmBlocks.begin(), warmBlocks.end(), 0.0f);
	std::fill(blockLabel.begin(), blockLabel.end(), 0);
	std::fill(occThread.begin(), occThread.end(), 0u);
	std::fill(occTime.begin(), occTime.end(), -1e9f);
	segs.clear();
	tipList.clear();
	removedIds.clear();
	nextSegmentId = 0;
	nextTipId = 1;
	boundaryCount = 0;
	tipsSpawned = fusions = connections = 0;
}

// ------------------------------------------------------------ patches
void MycorrhizaField::updatePatches(const Inputs & in, float dt, bool adopting)
{
	const int W = cols, H = rows, B = blockSize;
	const float aF = 1.0f - std::exp(-dt / std::max(1e-3f, TOUCH_FAST_SEC));
	const float aS = 1.0f - std::exp(-dt / std::max(1e-3f, TOUCH_SLOW_SEC));
	const float thr = std::max(0.0f, PATCH_CELL_MM);
	float * fast = cellFast.data();
	float * slow = cellSlow.data();
	unsigned char * mk = mark.data();
	unsigned char * tmp = markTmp.data();
	// 1. Running means and the raw mark.
	parallel((size_t)H, [&](size_t y0, size_t y1, int) {
		for (int y = (int)y0; y < (int)y1; y++) {
			const float * tb = touch.data() + (size_t)(y / B) * bw;
			for (int x = 0; x < W; x++) {
				const size_t i = (size_t)y * W + x;
				const float e = in.elevationMM[i];
				unsigned char m = 0;
				if (std::isfinite(e)) {
					if (adopting) {
						fast[i] = slow[i] = e;
					} else {
						fast[i] += aF * (e - fast[i]);
						slow[i] += aS * (e - slow[i]);
						if (tb[x / B] > 0.05f && !(in.water && in.water[i]) && std::fabs(fast[i] - slow[i]) > thr)
							m = 1;
					}
				}
				mk[i] = m;
			}
		}
	});
	// Passes 2-3 only inside the touched blocks' bounding box (+2 cells);
	// outside it the mark is 0 everywhere, so the closed mark and the
	// footprint are 0 too.
	int bx0 = bw, bx1 = -1, by0 = bh, by1 = -1;
	for (int by = 0; by < bh; by++)
		for (int bx = 0; bx < bw; bx++)
			if (touch[(size_t)by * bw + bx] > 0.05f) { bx0 = std::min(bx0, bx); bx1 = std::max(bx1, bx); by0 = std::min(by0, by); by1 = std::max(by1, by); }
	const bool any = bx1 >= 0 && !adopting;
	const int X0 = any ? std::max(0, bx0 * B - 2) : 0, X1 = any ? std::min(W, (bx1 + 1) * B + 2) : 0;
	const int Y0 = any ? std::max(0, by0 * B - 2) : 0, Y1 = any ? std::min(H, (by1 + 1) * B + 2) : 0;
	// 2. Close small gaps: 3 x 3 dilate (mark -> tmp), then 3 x 3 erode (tmp -> mark).
	if (any) parallel((size_t)(Y1 - Y0), [&](size_t r0, size_t r1, int) {
		for (int y = Y0 + (int)r0; y < Y0 + (int)r1; y++)
			for (int x = X0; x < X1; x++) {
				unsigned char v = 0;
				for (int dy = -1; dy <= 1 && !v; dy++) {
					const int yy = std::min(H - 1, std::max(0, y + dy));
					for (int dx = -1; dx <= 1; dx++) {
						const int xx = std::min(W - 1, std::max(0, x + dx));
						if (mk[(size_t)yy * W + xx]) { v = 1; break; }
					}
				}
				tmp[(size_t)y * W + x] = v;
			}
	});
	if (any) parallel((size_t)(Y1 - Y0), [&](size_t r0, size_t r1, int) {
		for (int y = Y0 + (int)r0; y < Y0 + (int)r1; y++)
			for (int x = X0; x < X1; x++) {
				unsigned char v = 1;
				for (int dy = -1; dy <= 1 && v; dy++) {
					const int yy = std::min(H - 1, std::max(0, y + dy));
					for (int dx = -1; dx <= 1; dx++) {
						const int xx = std::min(W - 1, std::max(0, x + dx));
						if (xx < X0 || xx >= X1 || yy < Y0 || yy >= Y1 || !tmp[(size_t)yy * W + xx]) { v = 0; break; }
					}
				}
				mk[(size_t)y * W + x] = v;
			}
	});
	// 3. Footprint: 3 x 3 box blur of the closed mark (the source of the light-liquid).
	float * fp = footprint.data();
	parallel((size_t)H, [&](size_t y0, size_t y1, int) {
		for (int y = (int)y0; y < (int)y1; y++)
			for (int x = 0; x < W; x++) {
				int sum = 0;
				if (any && x >= X0 && x < X1 && y >= Y0 && y < Y1)
					for (int dy = -1; dy <= 1; dy++) {
						const int yy = std::min(H - 1, std::max(0, y + dy));
						for (int dx = -1; dx <= 1; dx++)
							sum += mk[(size_t)yy * W + std::min(W - 1, std::max(0, x + dx))];
					}
				fp[(size_t)y * W + x] = sum / 9.0f;
			}
	});
	updateLiquid(in, dt);
}

// ------------------------------------------------------------ light-liquid (half resolution)
// Viscous shallow flow over the settled sand (cellSlow), at 2 x 2 cells:
// flux between neighbours = LIQUID_VISCOSITY x h_up^3 x (surface difference)
// x dt / dx^2 (h_up: depth on the higher-surface side), so thin edges hardly
// move and deep liquid flows like lava; each cell sends at most a quarter of
// its depth per substep (no negative depth). Reshaped sand (the footprint)
// adds LIQUID_SOURCE_MM_S x footprint; liquid evaporates to 10 % over the
// trace memory. It ignores simulated water: basins fill with light.
// Maturity is a tracer carried with the liquid (fresh liquid is 0, a new
// mark resets it), eased up toward the ground's life = max(total cover, S)
// (never down until the next mark), optionally blended with time since the
// mark (MATURITY_TIME_WEIGHT). Presence = smoothstep(0, LIQUID_PRESENCE_MM,
// depth), upsampled to cells with maturity.
void MycorrhizaField::updateLiquid(const Inputs & in, float dt)
{
	const int W = cols, H = rows, LW = lw, LH = lhh;
	const size_t ln = (size_t)LW * LH;
	const float dx = 2.0f * std::max(1e-3f, in.mmPerCell);
	const float now = (float)time;
	// 1. Terrain, source and life per half cell.
	parallel((size_t)LH, [&](size_t r0, size_t r1, int) {
		for (int j = (int)r0; j < (int)r1; j++)
			for (int i = 0; i < LW; i++) {
				const size_t l = (size_t)j * LW + i;
				float z = 0, src = 0, life = 0;
				const bool wet = ldepth[l] > 0.0f;
				for (int dy = 0; dy < 2; dy++)
					for (int dxi = 0; dxi < 2; dxi++) {
						const int x = std::min(W - 1, 2 * i + dxi), y = std::min(H - 1, 2 * j + dy);
						const size_t c = (size_t)y * W + x;
						z += cellSlow[c]; src += footprint[c];
						if (wet || footprint[c] > 0.0f) { // life only where there is (or will be) liquid
							float cv = 0;
							for (int r = 0; r < 3; r++) if (in.cover[r]) cv += in.cover[r][c];
							life += std::max(std::min(1.0f, cv), in.soil ? in.soil[c] : 0.0f);
						}
					}
				lz[l] = 0.25f * z; lsrc[l] = 0.25f * src; llife[l] = 0.25f * life;
			}
	});
	// 2. Source; a new mark resets maturity.
	const float add = std::max(0.0f, LIQUID_SOURCE_MM_S) * dt;
	for (size_t l = 0; l < ln; l++) {
		if (lsrc[l] <= 0.0f) continue;
		ldepth[l] += add * lsrc[l];
		if (lsrc[l] >= 0.5f) { lq[l] = 0.0f; lmark[l] = now; }
	}
	// 3. Flow, in substeps (Jacobi: fluxes from the state at the start of each substep).
	const int nsub = std::max(1, std::min(8, LIQUID_SUBSTEPS));
	const float dts = dt / nsub;
	const float kv = std::max(0.0f, LIQUID_VISCOSITY) * dts / (dx * dx);
	std::fill(lvx.begin(), lvx.end(), 0.0f);
	std::fill(lvy.begin(), lvy.end(), 0.0f);
	float * h = ldepth.data();
	float * q = lq.data();
	float * FR = lfr.data();
	float * FD = lfd.data();
	float * sc = lscale.data();
	for (int sub = 0; sub < nsub; sub++) {
		// a. Raw fluxes across the right and lower face of each cell (positive: out of this cell).
		parallel((size_t)LH, [&](size_t r0, size_t r1, int) {
			for (int j = (int)r0; j < (int)r1; j++)
				for (int i = 0; i < LW; i++) {
					const size_t l = (size_t)j * LW + i;
					const float s0 = lz[l] + h[l];
					const bool dry = h[l] <= 0.0f;
					auto flux = [&](size_t m) {
						const float ds = s0 - (lz[m] + h[m]);
						const float hu = ds > 0.0f ? h[l] : h[m];
						return kv * hu * hu * hu * ds;
					};
					// two dry cells exchange nothing
					FR[l] = i < LW - 1 && !(dry && h[l + 1] <= 0.0f) ? flux(l + 1) : 0.0f;
					FD[l] = j < LH - 1 && !(dry && h[l + LW] <= 0.0f) ? flux(l + LW) : 0.0f;
				}
		});
		// b. Outflow limit per cell: at most a quarter of its depth.
		parallel((size_t)LH, [&](size_t r0, size_t r1, int) {
			for (int j = (int)r0; j < (int)r1; j++)
				for (int i = 0; i < LW; i++) {
					const size_t l = (size_t)j * LW + i;
					float out = std::max(0.0f, FR[l]) + std::max(0.0f, FD[l]);
					if (i > 0) out += std::max(0.0f, -FR[l - 1]);
					if (j > 0) out += std::max(0.0f, -FD[l - LW]);
					sc[l] = out > 0.25f * h[l] ? (out > 0.0f ? 0.25f * h[l] / out : 0.0f) : 1.0f;
				}
		});
		// c. Apply (each face scaled by its source cell); tracer moves with the source's maturity.
		parallel((size_t)LH, [&](size_t r0, size_t r1, int) {
			for (int j = (int)r0; j < (int)r1; j++)
				for (int i = 0; i < LW; i++) {
					const size_t l = (size_t)j * LW + i;
					auto face = [&](float F, size_t a, size_t b) { // F > 0: a -> b
						const size_t src = F > 0.0f ? a : b;
						return F * sc[src];
					};
					auto conc = [&](size_t c) { return h[c] > 1e-6f ? q[c] / h[c] : 0.0f; };
					float dh = 0, dq = 0, fxR = 0, fxL = 0, fyD = 0, fyU = 0;
					if (i < LW - 1) { const float f = face(FR[l], l, l + 1); fxR = f; dh -= f; dq -= f * conc(f > 0 ? l : l + 1); }
					if (i > 0) { const float f = face(FR[l - 1], l - 1, l); fxL = f; dh += f; dq += f * conc(f > 0 ? l - 1 : l); }
					if (j < LH - 1) { const float f = face(FD[l], l, l + LW); fyD = f; dh -= f; dq -= f * conc(f > 0 ? l : l + LW); }
					if (j > 0) { const float f = face(FD[l - LW], l - LW, l); fyU = f; dh += f; dq += f * conc(f > 0 ? l - LW : l); }
					ltmp[l] = dh; lqtmp[l] = dq;
					// velocity (mm/s), mean over the substeps: face flux / depth
					if (h[l] > 0.05f) {
						lvx[l] += 0.5f * (fxL + fxR) * dx / (h[l] * dts) / nsub;
						lvy[l] += 0.5f * (fyU + fyD) * dx / (h[l] * dts) / nsub;
					}
				}
		});
		for (size_t l = 0; l < ln; l++) {
			h[l] = std::max(0.0f, h[l] + ltmp[l]);
			q[l] = std::max(0.0f, std::min(h[l], q[l] + lqtmp[l]));
		}
	}
	// 4. Evaporation, maturity, cut-off.
	const double mem = std::max(0.05f, TRACE_MEMORY_MIN) * 60.0;
	const float fade = (float)std::pow(0.1, dt / mem);
	const float aEase = 1.0f - std::exp(-dt / std::max(1e-3f, MATURITY_EASE_SEC));
	const float tw = std::min(1.0f, std::max(0.0f, MATURITY_TIME_WEIGHT));
	const float matureInv = 1.0f / std::max(0.1f, MATURE_SEC);
	for (size_t l = 0; l < ln; l++) {
		h[l] *= fade; q[l] *= fade;
		if (h[l] < LIQUID_CUTOFF_MM) { h[l] = 0.0f; q[l] = 0.0f; lmat[l] = 0.0f; continue; } // invisible film: dry
		float m = q[l] / h[l];
		if (lsrc[l] < 0.5f) {
			float target = llife[l];
			if (tw > 0.0f) target = (1.0f - tw) * target + tw * std::min(1.0f, (now - lmark[l]) * matureInv);
			if (target > m) m += (target - m) * aEase;
		}
		lmat[l] = m;
		q[l] = m * h[l];
	}
	// 5. Presence and maturity per cell (bilinear from the half grid).
	const float pInv = 1.0f / std::max(0.05f, LIQUID_PRESENCE_MM);
	float * P = presenceMap.data();
	float * Mt = maturityMap.data();
	parallel((size_t)H, [&](size_t y0, size_t y1, int) {
		for (int y = (int)y0; y < (int)y1; y++)
			for (int x = 0; x < W; x++) {
				const float fx = (x + 0.5f) * 0.5f - 0.5f, fy = (y + 0.5f) * 0.5f - 0.5f;
				const int i0 = std::max(0, std::min(LW - 1, (int)std::floor(fx))), j0 = std::max(0, std::min(LH - 1, (int)std::floor(fy)));
				const int i1 = std::min(LW - 1, i0 + 1), j1 = std::min(LH - 1, j0 + 1);
				const size_t c = (size_t)y * W + x;
				if (ldepth[(size_t)j0 * LW + i0] <= 0.0f && ldepth[(size_t)j0 * LW + i1] <= 0.0f &&
					ldepth[(size_t)j1 * LW + i0] <= 0.0f && ldepth[(size_t)j1 * LW + i1] <= 0.0f) { P[c] = 0.0f; Mt[c] = 0.0f; continue; }
				const float tx = std::min(1.0f, std::max(0.0f, fx - i0)), ty = std::min(1.0f, std::max(0.0f, fy - j0));
				auto bl = [&](const std::vector<float> & f) {
					return (f[(size_t)j0 * LW + i0] * (1 - tx) + f[(size_t)j0 * LW + i1] * tx) * (1 - ty) +
						(f[(size_t)j1 * LW + i0] * (1 - tx) + f[(size_t)j1 * LW + i1] * tx) * ty;
				};
				const float d = bl(ldepth) * pInv;
				const float t = std::min(1.0f, std::max(0.0f, d));
				P[c] = t * t * (3.0f - 2.0f * t);
				Mt[c] = P[c] > 0.0f ? bl(lmat) : 0.0f;
			}
	});
}

// ------------------------------------------------------------ attractant, warmth, labels (touch-block resolution)
static void boxBlur(std::vector<float> & f, std::vector<float> & tmp, int w, int h, int r)
{
	if (r <= 0 || w <= 0 || h <= 0) return;
	tmp.resize(f.size());
	for (int y = 0; y < h; y++) {
		const float * row = f.data() + (size_t)y * w;
		float * out = tmp.data() + (size_t)y * w;
		for (int x = 0; x < w; x++) {
			float s = 0; int n = 0;
			for (int k = std::max(0, x - r); k <= std::min(w - 1, x + r); k++) { s += row[k]; n++; }
			out[x] = s / n;
		}
	}
	for (int x = 0; x < w; x++)
		for (int y = 0; y < h; y++) {
			float s = 0; int n = 0;
			for (int k = std::max(0, y - r); k <= std::min(h - 1, y + r); k++) { s += tmp[(size_t)k * w + x]; n++; }
			f[(size_t)y * w + x] = s / n;
		}
}

void MycorrhizaField::updateAttractant(const Inputs & in)
{
	const int B = blockSize;
	std::vector<float> & A = attractBlocks;
	std::vector<float> & Wm = warmBlocks;
	const float * P = presenceMap.data();
	const float * Mt = maturityMap.data();
	std::fill(blockLabel.begin(), blockLabel.end(), 0);
	for (int by = 0; by < bh; by++)
		for (int bx = 0; bx < bw; bx++) {
			float sp = 0, sc = 0, sw = 0, pmax = 0; int n = 0;
			for (int y = by * B; y < std::min(rows, by * B + B); y++)
				for (int x = bx * B; x < std::min(cols, bx * B + B); x++) {
					const size_t i = (size_t)y * cols + x;
					sp += P[i]; sw += P[i] * Mt[i]; pmax = std::max(pmax, P[i]);
					float c = 0;
					for (int r = 0; r < 3; r++) if (in.cover[r]) c += in.cover[r][i];
					sc += std::min(1.0f, c);
					n++;
				}
			const size_t b = (size_t)by * bw + bx;
			A[b] = n ? (sp + ATTRACT_PLANT * sc) / n : 0.0f;
			Wm[b] = n ? sw / n : 0.0f;
			blockLabel[b] = pmax >= 0.5f ? -1 : 0; // -1: patch block, not yet labelled
		}
	for (int pass = 0; pass < 2; pass++) {
		boxBlur(A, blurTmp, bw, bh, ATTRACT_BLUR_BLOCKS);
		boxBlur(Wm, blurTmp, bw, bh, 1);
	}
	labelPatchBlocks();
}

// 4-connected labels of patch blocks (1, 2, ...); 0 = no patch.
void MycorrhizaField::labelPatchBlocks()
{
	int next = 1;
	for (size_t s = 0; s < blockLabel.size(); s++) {
		if (blockLabel[s] != -1) continue;
		labelQueue.clear();
		labelQueue.push_back((int)s);
		blockLabel[s] = next;
		for (size_t q = 0; q < labelQueue.size(); q++) {
			const int b = labelQueue[q], x = b % bw, y = b / bw;
			const int nb[4] = { x > 0 ? b - 1 : -1, x < bw - 1 ? b + 1 : -1, y > 0 ? b - bw : -1, y < bh - 1 ? b + bw : -1 };
			for (int k = 0; k < 4; k++)
				if (nb[k] >= 0 && blockLabel[nb[k]] == -1) { blockLabel[nb[k]] = next; labelQueue.push_back(nb[k]); }
		}
		next++;
	}
}

// Bilinear sample of a touch-block field at grid cell coordinate (gx, gy).
float MycorrhizaField::sampleBlocks(const std::vector<float> & f, float gx, float gy) const
{
	const float fx = gx / blockSize - 0.5f, fy = gy / blockSize - 0.5f;
	int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
	const float tx = fx - x0, ty = fy - y0;
	auto at = [&](int x, int y) { x = std::max(0, std::min(bw - 1, x)); y = std::max(0, std::min(bh - 1, y)); return f[(size_t)y * bw + x]; };
	return (at(x0, y0) * (1 - tx) + at(x0 + 1, y0) * tx) * (1 - ty) + (at(x0, y0 + 1) * (1 - tx) + at(x0 + 1, y0 + 1) * tx) * ty;
}

// ------------------------------------------------------------ hyphae
static float widthFor(int gen)
{
	const float hi = std::max(0.1f, MycorrhizaField::HYPHA_WIDTH_PX);
	const float lo = std::min(hi, std::max(0.1f, MycorrhizaField::HYPHA_MIN_WIDTH_PX));
	return lo + (hi - lo) * std::pow(0.55f, (float)gen);
}

void MycorrhizaField::spawnTips(const Inputs & in, float dt)
{
	const int W = cols, H = rows;
	const float * P = presenceMap.data();
	const float perCell = std::max(0.0f, HYPHA_SPAWN_PER_SEC) * std::max(1e-3f, in.mmPerCell) * dt;
	const float jitter = SPAWN_JITTER_DEG * L_DEG;
	boundaryCount = 0;
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			const size_t i = (size_t)y * W + x;
			if (P[i] < 0.5f || (in.water && in.water[i])) continue;
			const bool edge = (x > 0 && P[i - 1] < 0.5f) || (x < W - 1 && P[i + 1] < 0.5f) ||
				(y > 0 && P[i - W] < 0.5f) || (y < H - 1 && P[i + W] < 0.5f);
			if (!edge) continue;
			boundaryCount++;
			const float boost = 1.0f + std::max(0.0f, HYPHA_TOUCH_BOOST) * touch[(size_t)(y / blockSize) * bw + x / blockSize];
			if ((int)tipList.size() >= MAX_TIPS || rnd01() >= perCell * P[i] * boost) continue;
			const float gx = P[i - (x > 0 ? 1 : 0)] - P[i + (x < W - 1 ? 1 : 0)];
			const float gy = P[i - (y > 0 ? W : 0)] - P[i + (y < H - 1 ? W : 0)];
			const float a = std::atan2(gy, gx) + (rnd01() * 2.0f - 1.0f) * jitter;
			Tip t;
			t.x = t.sx = t.bx = x + 0.5f;
			t.y = t.sy = t.by = y + 0.5f;
			t.hx = std::cos(a); t.hy = std::sin(a);
			t.age = 0.0f; t.grown = 0.0f; t.run = 0.0f;
			t.gen = 0; t.width = widthFor(0);
			t.id = nextTipId++; t.parent = 0; t.outside = false;
			tipList.push_back(t);
			tipsSpawned++;
		}
}

void MycorrhizaField::growTips(const Inputs & in, float dt)
{
	const float mm = std::max(1e-3f, in.mmPerCell);
	const float minMob = std::min(1.0f, std::max(0.0f, MIN_MOBILITY));
	const float probe = PROBE_MM / mm;
	const float side = 35.0f * L_DEG, cS = std::cos(side), sS = std::sin(side);
	const float noise = TURN_NOISE_DEG * L_DEG;
	const float occValid = (float)(0.7 * std::max(0.05f, TRACE_MEMORY_MIN) * 60.0); // fade ~0.2
	const float now = (float)time;
	const float * P = presenceMap.data();
	const int W = cols, H = rows, B = blockSize;
	auto offGrid = [&](float x, float y) { return x < 0.0f || y < 0.0f || x >= W || y >= H; };
	auto blocked = [&](float x, float y) { // water (or off the grid)
		if (offGrid(x, y)) return true;
		return in.water && in.water[(size_t)(int)y * W + (int)x];
	};
	auto labelAt = [&](float x, float y) {
		const int bx = std::min(bw - 1, std::max(0, (int)x / B)), by = std::min(bh - 1, std::max(0, (int)y / B));
		return blockLabel[(size_t)by * bw + bx];
	};
	auto pushSeg = [&](float x0, float y0, float x1, float y1, float w) {
		if ((int)segs.size() >= std::max(1, MAX_SEGMENTS)) segs.pop_front();
		Segment s{ x0, y0, x1, y1, w, now, nextSegmentId++ };
		segs.push_back(s);
	};
	std::vector<Tip> born;
	for (size_t i = tipList.size(); i-- > 0;) {
		Tip & t = tipList[i];
		bool done = false;
		const size_t here = (size_t)(int)t.y * W + (int)t.x;
		t.age += dt;
		if (t.age > TIP_LIFE_SEC) done = true;
		if (!done) {
			float g = 1.0f;
			if (in.soil || in.moisture) {
				g = std::max(in.soil ? in.soil[here] : 0.0f, in.moisture ? in.moisture[here] : 0.0f);
				g = std::min(1.0f, std::max(0.0f, g));
			}
			const float dmm = std::max(0.0f, HYPHA_SPEED_MM_S) * (minMob + (1.0f - minMob) * g) * dt;
			const float dc = dmm / mm;
			// Persistent random walk.
			const float r = gauss() * noise * std::sqrt(dmm);
			float hx = t.hx * std::cos(r) - t.hy * std::sin(r), hy = t.hx * std::sin(r) + t.hy * std::cos(r);
			// Pull toward other patches and plant cover.
			if (t.grown > HOME_MM) {
				const float e = (float)B;
				const float ax = (sampleBlocks(attractBlocks, t.x + e, t.y) - sampleBlocks(attractBlocks, t.x - e, t.y)) / (2 * e * mm);
				const float ay = (sampleBlocks(attractBlocks, t.x, t.y + e) - sampleBlocks(attractBlocks, t.x, t.y - e)) / (2 * e * mm);
				const float am = std::sqrt(ax * ax + ay * ay);
				if (am > 1e-7f) {
					const float s = ATTRACT_WEIGHT * dmm * std::min(1.0f, am * 30.0f) / am;
					hx += s * ax; hy += s * ay;
				}
			}
			float len = std::sqrt(hx * hx + hy * hy);
			hx /= len; hy /= len;
			// Stop at the grid edge; steer away from water.
			if (offGrid(t.x + hx * probe, t.y + hy * probe)) done = true;
			else if (blocked(t.x + hx * probe, t.y + hy * probe)) {
				const float lx = hx * cS - hy * sS, ly = hx * sS + hy * cS;
				const float rx = hx * cS + hy * sS, ry = -hx * sS + hy * cS;
				const bool lb = blocked(t.x + lx * probe, t.y + ly * probe), rb = blocked(t.x + rx * probe, t.y + ry * probe);
				if (lb && rb) done = true;
				else if (!lb && (rb || (rnd() & 1))) { hx = lx; hy = ly; }
				else { hx = rx; hy = ry; }
			}
			t.hx = hx; t.hy = hy;
			const float nx = t.x + hx * dc, ny = t.y + hy * dc;
			if (!done && blocked(nx, ny)) done = true;
			if (!done) {
				const size_t ni = (size_t)(int)ny * W + (int)nx;
				const bool inPatch = P[ni] >= 0.5f;
				if (!inPatch) t.outside = true;
				else if (!t.outside && t.run > LEAVE_HOME_MM) done = true; // born heading inward: never left home
				bool turnedBack = false;
				if (inPatch && t.outside) {
					const int home = labelAt(t.bx, t.by), cur = labelAt(nx, ny);
					if (home != 0 && cur == home) { // do not re-enter the home patch: turn back, stay this step
						t.hx = -hx; t.hy = -hy;
						turnedBack = true;
					} else {
						pushSeg(t.sx, t.sy, nx, ny, t.width); // reached another patch: a connection
						connections++;
						done = true;
					}
				}
				if (!done && !turnedBack) {
					// Fuse with another thread.
					const size_t o = (size_t)((int)ny / 2) * ow + (int)nx / 2;
					// Only threads laid down more than FUSE_MIN_AGE_SEC ago count: a fresh
					// branch shares cells with its parent at the branch point.
					if (t.run > FUSE_AFTER_MM && occThread[o] != 0 && occThread[o] != t.id && occThread[o] != t.parent &&
						now - occTime[o] < occValid && now - occTime[o] > FUSE_MIN_AGE_SEC) {
						pushSeg(t.sx, t.sy, nx, ny, t.width);
						fusions++;
						done = true;
					} else {
						if (occThread[o] != t.id) { occThread[o] = t.id; occTime[o] = now; } // keep the time this thread first passed
						t.x = nx; t.y = ny;
						t.grown += dmm; t.run += dmm;
						const float sl = std::hypot(t.x - t.sx, t.y - t.sy) * mm;
						if (sl >= SEGMENT_MM) { pushSeg(t.sx, t.sy, t.x, t.y, t.width); t.sx = t.x; t.sy = t.y; }
						// Branch.
						if ((int)(tipList.size() + born.size()) < MAX_TIPS && t.gen < MAX_GENERATION && rnd01() < BRANCH_PER_MM * dmm) {
							const float a = ((rnd() & 1) ? 1.0f : -1.0f) * (30.0f + 30.0f * rnd01()) * L_DEG;
							Tip c = t;
							c.hx = hx * std::cos(a) - hy * std::sin(a);
							c.hy = hx * std::sin(a) + hy * std::cos(a);
							c.sx = c.x; c.sy = c.y;
							c.age = 0.0f; c.run = 0.0f;
							c.gen = t.gen + 1; c.width = widthFor(c.gen);
							c.id = nextTipId++; c.parent = t.id;
							born.push_back(c);
							tipsSpawned++;
						}
					}
				}
			}
		}
		if (done) {
			Tip & d = tipList[i];
			if (std::hypot(d.x - d.sx, d.y - d.sy) > 0.2f) pushSeg(d.sx, d.sy, d.x, d.y, d.width);
			tipList[i] = tipList.back();
			tipList.pop_back();
		}
	}
	tipList.insert(tipList.end(), born.begin(), born.end());
}

void MycorrhizaField::pruneSegments(const Inputs & in)
{
	removedIds.clear();
	while (!segs.empty() && fadeAt(segs.front().birth) < GONE_BRIGHTNESS)
		segs.pop_front();
	if (!in.water)
		return;
	const int W = cols, H = rows;
	auto wet = [&](float x, float y) {
		const int xi = std::min(W - 1, std::max(0, (int)x)), yi = std::min(H - 1, std::max(0, (int)y));
		return in.water[(size_t)yi * W + xi] != 0;
	};
	for (Segment & s : segs)
		if (s.width > 0.0f && (wet(s.x0, s.y0) || wet(s.x1, s.y1))) {
			s.width = 0.0f;
			removedIds.push_back(s.id);
		}
}
