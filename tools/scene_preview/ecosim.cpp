// Headless ecosystem for the Scene preview's bit-identity check (copied
// from the Prototype 3 ecosystem harness, 2026-11-17): the real hydrology
// (VegetationField::stepFlow / addSourceInflow / addSeepage, copied
// verbatim by tools/pipe_particles_harness/extract.py and compiled against
// its stub.h) and the real SuccessionModel.cpp, in the app's frame order
// (VegetationField::update). Built as a shared library (build.sh) and
// driven from scene_preview.py through ctypes; the Scene pipeline only
// reads the arrays below.
#include "../pipe_particles_harness/stub.h"
#include "../../src/Games/SuccessionModel.h"
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>
#include <string>

double g_frameDt = 1.0 / 40.0;
#include "../pipe_particles_harness/gen/defaults.inc"
#include "../pipe_particles_harness/gen/stepflow.inc"

typedef SuccessionModel SM;

static std::string tag(const std::string & block, const std::string & name)
{
	std::string open = "<" + name + ">", close = "</" + name + ">";
	size_t a = block.find(open), b = block.find(close);
	if (a == std::string::npos || b == std::string::npos) return "";
	return block.substr(a + open.size(), b - a - open.size());
}

// The Designer preset, as EcologyParams loads it (VegetationField::setup).
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

struct EcoSim {
	int W, H;
	float mmPerCell;
	VegetationField v;
	SM m;
	std::vector<float> bed, noisy, waterState, above;
	std::vector<unsigned char> water;
	std::mt19937 rng;
	float spont[3];
	EcoSim(int w, int h, float mm, unsigned seed) : W(w), H(h), mmPerCell(mm), bed(w * h, 0.0f), noisy(w * h),
		waterState(w * h, 0.0f), above(w * h), water(w * h, 0), rng(seed) { v.init(h, w); }

	void frame(float dt, float noiseMM, int succession) {
		g_frameDt = dt;
		std::uniform_real_distribution<float> U(-noiseMM, noiseMM);
		for (int i = 0; i < W * H; i++) noisy[i] = bed[i] + (noiseMM > 0.0f ? U(rng) : 0.0f);
		v.previousElevation.d = noisy;
		// Water classification from last frame's depth (VegetationField::update).
		const float on = VegetationField::MIN_VISIBLE_WATER_DEPTH_FRAC + VegetationField::WATER_HYSTERESIS_FRAC;
		const float off = VegetationField::MIN_VISIBLE_WATER_DEPTH_FRAC;
		for (int i = 0; i < W * H; i++) {
			const float d = v.waterDepth.d[i];
			waterState[i] = (waterState[i] > 0.5f ? d > off : d > on) ? 1.0f : 0.0f;
		}
		if (succession) {
			// VegetationField::stepSuccession's inputs.
			const float * terr = v.flowTerrainReady ? v.flowTerrain.d.data() : noisy.data();
			for (int i = 0; i < W * H; i++) {
				water[i] = waterState[i] > 0.5f ? 1 : 0;
				above[i] = terr[i] - VegetationField::WATER_TABLE_MM;
			}
			SM::Inputs in;
			in.elevationMM = noisy.data(); in.water = water.data(); in.heightAboveTableMM = above.data();
			in.cols = W; in.rows = H; in.mmPerCell = mmPerCell; in.dt = dt;
			in.temperature = 0.745f; in.restingTemperature = 0.745f; // BASE_TEMPERATURE, untouched sand
			in.spreadRadiusMM = 40.0f;                                 // SPREAD_RADIUS_MM
			for (int r = 0; r < 3; r++) in.spontaneousSeed[r] = spont[r];
			m.step(in);
		}
		v.stepFlow(0.0f);
	}
};

extern "C" {
// spontaneous: FRUIT/NUT/SHRUB_GROWTH_CHANCE_PCT / 100 (pioneer, intermediate, climax).
void * eco_new(int w, int h, float mmPerCell, unsigned seed, const char * presetPath, float sp0, float sp1, float sp2)
{
	EcoSim * s = new EcoSim(w, h, mmPerCell, seed);
	s->spont[0] = sp0; s->spont[1] = sp1; s->spont[2] = sp2;
	SM::RoleParams roles[3];
	std::string error;
	if (!readPreset(presetPath, roles) || !s->m.setParams(roles, error)) {
		fprintf(stderr, "ecosim: preset refused: %s\n", error.c_str());
		delete s;
		return nullptr;
	}
	return s;
}
void eco_free(void * p) { delete (EcoSim *)p; }
void eco_frame(void * p, float dt, float noiseMM, int succession) { ((EcoSim *)p)->frame(dt, noiseMM, succession); }
float * eco_bed(void * p) { return ((EcoSim *)p)->bed.data(); }
const float * eco_elevation(void * p) { return ((EcoSim *)p)->noisy.data(); }
const float * eco_water_depth(void * p) { return ((EcoSim *)p)->v.waterDepth.d.data(); } // fraction of range, as getWaterDepthAt
const float * eco_water_state(void * p) { return ((EcoSim *)p)->waterState.data(); }    // isWaterCell
const float * eco_vel_x(void * p) { return ((EcoSim *)p)->v.velX.d.data(); }
const float * eco_vel_y(void * p) { return ((EcoSim *)p)->v.velY.d.data(); }
int eco_succession_ready(void * p) { EcoSim * s = (EcoSim *)p; return s->m.getCols() == s->W && s->m.getRows() == s->H; }
const float * eco_cover(void * p, int role) { return ((EcoSim *)p)->m.cover(role); }
const float * eco_soil(void * p) { return ((EcoSim *)p)->m.soil(); }
const float * eco_moisture(void * p) { return ((EcoSim *)p)->m.moisture(); }
float eco_range_mm(void * p) { EcoSim * s = (EcoSim *)p; return s->v.elevationMax - s->v.elevationMin; }
}
