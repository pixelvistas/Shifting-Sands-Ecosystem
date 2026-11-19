// Headless check of src/Games/LeniaSpecies.* (branch prototype/6-lenia):
// loads bin/data/lenia/species.json with the app's own parser, assembles
// each species (and each world) exactly as LeniaLab does, and writes what
// the GPU receives (tap table, selection matrices, growth uniforms, cells)
// for the GLSL harness (lenia_lab.py).
//
//   ./build.sh && ./species_tool list ../../bin/data/lenia/species.json
//   ./species_tool dump ../../bin/data/lenia/species.json out/setup
#include "../../src/Games/LeniaSpecies.h"

#include <cstdio>
#include <fstream>
#include <string>

using namespace lenia;

static void writeSetup(const std::string & path, const std::string & id, const WorldSetup & w,
                       const std::vector<Grid> & cells, int noisePatch, double rScale)
{
	std::ofstream f(path);
	f.precision(9);
	f << "{\"id\":\"" << id << "\",\"model\":" << w.model << ",\"embed\":" << (w.embed ? 1 : 0)
	  << ",\"dt\":" << w.dt << ",\"dd\":" << w.dd << ",\"sigma\":" << w.sigma << ",\"theta\":" << w.theta
	  << ",\"rScale\":" << rScale << ",\"noisePatch\":" << noisePatch
	  << ",\"nKernels\":" << w.kernels.size() << ",\"nTaps\":" << w.table.count << ",\"maxRadius\":" << w.table.maxRadius
	  << ",\"maxTruncated\":" << w.maxTruncated;
	auto arr = [&](const char * name, const float * v, int n) {
		f << ",\"" << name << "\":[";
		for (int i = 0; i < n; i++) f << (i ? "," : "") << v[i];
		f << "]";
	};
	arr("m", w.m, MAX_KERNELS); arr("s", w.s, MAX_KERNELS); arr("h", w.h, MAX_KERNELS);
	arr("gn", w.gn, MAX_KERNELS); arr("rel", w.rel, MAX_KERNELS); arr("channelOn", w.channelOn, MAX_CHANNELS);
	for (int g = 0; g < 3; g++) {
		arr(("srcSel" + std::to_string(g)).c_str(), w.srcSel[g], 16);
		arr(("dstSel" + std::to_string(g)).c_str(), w.dstSel[g], 16);
		arr(("slotSel" + std::to_string(g)).c_str(), w.slotSel[g], 16);
	}
	f << ",\"kernelR\":[";
	for (size_t k = 0; k < w.kernels.size(); k++) f << (k ? "," : "") << w.kernels[k].R;
	f << "],\"kernelRadius\":[";
	for (size_t k = 0; k < w.taps.size(); k++) f << (k ? "," : "") << w.taps[k].radius;
	f << "],\"tapDx\":[";
	for (int t = 0; t < w.table.count; t++) f << (t ? "," : "") << w.table.dx[t];
	f << "],\"tapDy\":[";
	for (int t = 0; t < w.table.count; t++) f << (t ? "," : "") << w.table.dy[t];
	f << "],\"tapW\":[";
	for (size_t i = 0; i < w.table.w.size(); i++) f << (i ? "," : "") << w.table.w[i];
	f << "],\"cells\":[";
	for (size_t c = 0; c < cells.size(); c++) {
		f << (c ? "," : "") << "{\"w\":" << cells[c].w << ",\"h\":" << cells[c].h << ",\"v\":[";
		for (size_t i = 0; i < cells[c].v.size(); i++) f << (i ? "," : "") << cells[c].v[i];
		f << "]}";
	}
	f << "]}\n";
}

int main(int argc, char ** argv)
{
	if (argc < 3) { std::fprintf(stderr, "usage: species_tool list|dump species.json [outdir]\n"); return 2; }
	const std::string cmd = argv[1];
	Library lib;
	std::string err;
	if (!loadLibraryFile(argv[2], lib, err)) { std::fprintf(stderr, "load failed: %s\n", err.c_str()); return 1; }
	int bad = 0;
	for (const Species & s : lib.species) {
		WorldConfig cfg;
		cfg.model = s.model;
		cfg.channelSpecies[0] = s.id;
		WorldSetup w;
		if (!assembleWorld(lib, cfg, w, err)) { std::printf("%-22s ASSEMBLY FAILED: %s\n", s.id.c_str(), err.c_str()); bad++; continue; }
		int cw = s.cells.empty() ? 0 : s.cells[0].w, ch = s.cells.empty() ? 0 : s.cells[0].h;
		std::printf("%-22s %-9s %s ch %d kernels %2zu taps %4d radius %2d trunc %.4f cells %dx%d dt %.3f\n",
			s.id.c_str(), s.status.c_str(), s.model == MODEL_FLOW ? "flow " : "lenia", s.channels, w.kernels.size(),
			w.table.count, w.table.maxRadius, w.maxTruncated, cw, ch, w.dt);
		if (cmd == "dump" && argc > 3)
			writeSetup(std::string(argv[3]) + "/" + s.id + ".json", s.id, w, s.cells, s.noisePatch, w.rScale);
	}
	for (const WorldDef & d : lib.worlds) {
		WorldConfig cfg;
		cfg.model = d.model;
		cfg.embed = d.embed;
		for (size_t c = 0; c < d.channelSpecies.size(); c++) cfg.channelSpecies[c] = d.channelSpecies[c];
		cfg.crossKernels = d.crossKernels;
		WorldSetup w;
		if (!assembleWorld(lib, cfg, w, err)) { std::printf("world %-16s ASSEMBLY FAILED: %s\n", d.id.c_str(), err.c_str()); bad++; continue; }
		std::printf("world %-16s %-9s kernels %2zu taps %4d radius %2d\n", d.id.c_str(), d.status.c_str(), w.kernels.size(), w.table.count, w.table.maxRadius);
		if (cmd == "dump" && argc > 3) {
			std::vector<Grid> cells;
			for (const auto & id : d.channelSpecies) {
				int si = lib.find(id);
				cells.push_back(si >= 0 && !lib.species[si].cells.empty() ? lib.species[si].cells[0] : Grid());
			}
			writeSetup(std::string(argv[3]) + "/world_" + d.id + ".json", d.id, w, cells, 0, w.rScale);
		}
	}
	// transformGrid sanity: rotating by 0 at scale 1 keeps the mass
	if (!lib.species.empty() && !lib.species[0].cells.empty()) {
		const Grid & g = lib.species[0].cells[0];
		double m0 = 0, m1 = 0, m2 = 0;
		for (float v : g.v) m0 += v;
		for (float v : transformGrid(g, 1.0, 0.0).v) m1 += v;
		for (float v : transformGrid(g, 1.0, 37.0).v) m2 += v;
		std::printf("transformGrid mass: %.4f, rot 0: %.4f, rot 37: %.4f\n", m0, m1, m2);
	}
	return bad ? 1 : 0;
}
