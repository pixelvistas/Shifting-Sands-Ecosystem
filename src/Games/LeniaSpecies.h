/***********************************************************************
LeniaSpecies - the Lenia lab's species library (plain C++, no
openFrameworks): reads bin/data/lenia/species.json, decodes Chan's RLE
cell patterns, and builds the convolution kernels as tap tables for the
GPU (src/Games/LeniaLab.*).

Two kernel families, as published:
- "shells" (Lenia; Chan 2019, Complex Systems 28(3); code
  github.com/Chakazul/Lenia, Python/LeniaND.py and LeniaNDKC.py):
  K(d) = b[floor(B q)] * core(B q mod 1) for q = d / (R r) < 1, with B
  shells of weights b, and core kn = 1 polynomial (4q(1-q))^4, 2
  exponential exp(4 - 1/(q(1-q))), 3 step (1/4 <= q <= 3/4). The index
  is the one LeniaND.py computes with (kernel_core[kn - 1]; its GUI
  labels name kn 1 "Exponential", the code computes the polynomial).
- "rings" (Flow-Lenia; Plantec et al. 2023, ALIFE, arXiv 2212.07906;
  code github.com/erwanplantec/FlowLenia, flowlenia/utils.py):
  D = d / ((R + 15) r), K = sigmoid(-10 (D - 1)) sum_j b_j
  exp(-(D - a_j)^2 / w_j). The sigmoid tail is cut at D = RINGS_CUT; the
  weight lost is reported (TapKernel::truncated).
Both are normalised to sum 1 over the taps kept.

Cross kernels may be "relative" (a lab extension, see Kernel::relative).

Growth (Chan's gn): 1 polynomial 2 max(0, 1 - (u-m)^2 / (9 s^2))^4 - 1,
2 Gaussian 2 exp(-(u-m)^2 / (2 s^2)) - 1 (Flow-Lenia's only growth),
3 step 2 [|u - m| <= s] - 1.

Not compiled with openFrameworks here; compiled and tested headless
(tools/lenia_lab/).
***********************************************************************/

#pragma once
#include <cstdlib>
#include <string>
#include <vector>

namespace lenia {

enum Model { MODEL_LENIA = 0, MODEL_FLOW = 1 };
enum Shape { SHAPE_SHELLS = 0, SHAPE_RINGS = 1 };
enum Core { CORE_POLY = 1, CORE_EXP = 2, CORE_STEP = 3 };
enum Growth { GROWTH_POLY = 1, GROWTH_GAUSS = 2, GROWTH_STEP = 3 };

static const int MAX_CHANNELS = 3;
static const int MAX_KERNELS = 12;     // per world (GPU: three RGBA weight rows)
static const int MAX_TAPS = 4096;      // union of kernel offsets (radius ~36 cells)
static const double RINGS_CUT = 1.6;   // D beyond which a rings kernel is cut

struct Kernel {
	int from = 0, to = 0;           // source / target channel (species-local in the file)
	int shape = SHAPE_SHELLS;
	double R = 13;                  // the species' R (set on load; cross kernels: own "R" or the target's)
	// shells
	std::vector<double> b = { 1.0 };
	double r = 1.0;                 // relative radius (LeniaNDKC 'r'; Flow-Lenia r)
	int kn = CORE_POLY;
	// rings (Flow-Lenia a, b, w; three bumps)
	double ra[3] = { 0, 0, 0 }, rb[3] = { 0, 0, 0 }, rw[3] = { 1, 1, 1 };
	// growth
	double m = 0.15, s = 0.015, h = 1.0;
	int gn = GROWTH_POLY;
	// Lab extension (not in Chan's or Plantec's code), for cross kernels:
	// contributes h (G(U) - G(0)), so an absent species contributes nothing,
	// and is left out of the sum of h that Lenia divides by.
	bool relative = false;
};

struct Grid {
	int w = 0, h = 0;
	std::vector<float> v;           // row-major, row 0 = first RLE row
	float at(int x, int y) const { return (x < 0 || y < 0 || x >= w || y >= h) ? 0.0f : v[(size_t)y * w + x]; }
};

struct Species {
	std::string id, name, status, source, notes;
	int model = MODEL_LENIA;
	int channels = 1;
	double R = 13;                  // Lenia: kernel radius in cells; Flow-Lenia: R of (R + 15) r
	double dt = 0.1;                // Lenia: 1 / T; Flow-Lenia: dt
	// Flow-Lenia (defaults are the published code's)
	int dd = 5;
	double sigma = 0.65, theta = 1.0;
	std::vector<Kernel> kernels;
	std::vector<Grid> cells;        // one per channel (may be empty: seed with noise)
	int noisePatch = 0;             // no cells: spawn a uniform-noise square of this side (cells)
	bool published() const { return status == "published"; }
};

// A world combination from the file ("worlds"): species per channel plus
// extra kernels between channels (world channel indices).
struct WorldDef {
	std::string id, name, status, source, notes;
	int model = MODEL_LENIA;
	bool embed = false;             // flow: species share channel 0; P carries the slot
	std::vector<std::string> channelSpecies;   // species id per channel / slot ("" = none)
	std::vector<Kernel> crossKernels;
};

struct Library {
	std::vector<Species> species;
	std::vector<WorldDef> worlds;
	int find(const std::string & id) const;
	int findWorld(const std::string & id) const;
};

bool parseLibrary(const std::string & jsonText, Library & out, std::string & err);
bool loadLibraryFile(const std::string & path, Library & out, std::string & err);

// Chan's 2-D RLE (Board.rle2arr): '.'/'b' 0, 'o' 255, 'A'..'X' 1..24,
// 'p'..'y' + 'A'..'X' 25..255 (value / 255), digits = run count, '$' row
// end (count = repeated empty rows), '!' end. Rows are padded to the widest.
bool decodeRLE(const std::string & rle, Grid & g, std::string & err);

double coreFn(int kn, double q);
double growthFn(int gn, double u, double m, double s);

// A kernel on integer offsets, normalised to sum 1.
struct TapKernel {
	int radius = 0;                 // weights cover [-radius, radius]^2
	std::vector<float> w;           // (2 radius + 1)^2, row-major (dy, dx)
	double truncated = 0.0;         // fraction of weight cut (rings only)
	double effectiveRadius = 0.0;   // cells, where the kernel reaches zero / is cut
	float at(int dx, int dy) const { return (std::abs(dx) > radius || std::abs(dy) > radius) ? 0.0f : w[(size_t)(dy + radius) * (2 * radius + 1) + dx + radius]; }
};
// rScale multiplies the kernel's radius (the lab's "R scale").
TapKernel buildKernel(const Kernel & k, double R, double rScale);

// The GPU's tap table: the union of every kernel's non-zero offsets, with
// each kernel's weight per tap (0 where a kernel does not reach).
struct TapTable {
	int count = 0;
	int kernels = 0;
	std::vector<int> dx, dy;
	std::vector<float> w;           // count * MAX_KERNELS, tap-major
	int maxRadius = 0;
};
bool buildTapTable(const std::vector<TapKernel> & ks, TapTable & t, std::string & err);

// Resample a pattern by `scale` (bilinear), rotated by `deg` about its centre.
Grid transformGrid(const Grid & g, double scale, double deg);

// ------------------------------------------------------------ world
// What the lab runs: one species per channel (or, with embedding, per
// slot, all in channel 0), plus cross kernels between channels.
struct WorldConfig {
	int model = MODEL_LENIA;
	bool embed = false;
	std::string channelSpecies[MAX_CHANNELS];
	std::vector<Kernel> crossKernels;   // world channel indices
};

// Everything the GPU needs, in world channels. assembleWorld fills
// kernels (and dt etc. from the first species); finalizeWorld (also after
// GUI edits of the kernels) builds the taps and the uniform arrays.
struct WorldSetup {
	int model = MODEL_LENIA;
	bool embed = false;
	std::vector<Kernel> kernels;
	std::vector<int> kernelSlot;        // embedding slot of each kernel (-1: none)
	std::vector<int> kernelOwner;       // channel (slot) whose species owns it; -1 cross kernel
	double rScale = 1.0;
	double dt = 0.1;
	int dd = 5;
	double sigma = 0.65, theta = 1.0;
	// finalizeWorld:
	std::vector<TapKernel> taps;
	TapTable table;
	float srcSel[3][16], dstSel[3][16], slotSel[3][16];   // column-major mat4 (GLSL)
	float m[MAX_KERNELS], s[MAX_KERNELS], h[MAX_KERNELS], gn[MAX_KERNELS], rel[MAX_KERNELS];
	float channelOn[MAX_CHANNELS];
	double maxTruncated = 0.0;
};
bool assembleWorld(const Library & lib, const WorldConfig & cfg, WorldSetup & out, std::string & err);
bool finalizeWorld(WorldSetup & w, std::string & err);

} // namespace lenia
