#include "LeniaLab.h"
#include "ofxImGui.h"
#include "glm/gtc/type_ptr.hpp"

using namespace lenia;

// Defaults (DEVLOG, 2026-11-19, prototype/6-lenia). Sizes and speeds of each
// preset at these values are in the DEVLOG; tune on the sand ("Save preset").
static LeniaLab::Params makeDefaults()
{
	LeniaLab::Params p;
	p.pxPerMm = 1.17f;          // stand-in (tools/sandbox_dither_preview); check with the 100 mm scale bar
	p.cellMm = 2.0f;            // Orbium (R 13) about 30-38 mm across
	p.rScale = 1.0f;
	p.timeScale = 1.5f;         // Orbium about 18 mm/s
	p.dt = 0.1f;
	p.wrapWorld = false;        // walls: the box edges
	p.maxStepsPerFrame = 8;
	p.paused = false;
	p.tool = LeniaLab::TOOL_SPAWN;
	p.spawnChannel = 0;
	p.spawnDeg = 0.0f;
	p.spawnRandomRot = true;
	p.brushMm = 25.0f;
	p.brushAmount = 1.0f;
	p.scatterCount = 6;
	p.noiseMm = 40.0f;
	p.maskMode = LeniaLab::MASK_NONE;
	p.maskRadiusMm = 250.0f;
	p.maskEdgeMm = 10.0f;
	p.maskCentreX = 0.5f;
	p.maskCentreY = 0.5f;
	p.outsideGrowth = 0.0f;     // as specified: growth zero outside
	p.edgeRepel = 0.0f;
	p.maskShow = 0.25f;
	p.gradMode = LeniaLab::GRAD_OFF;
	p.gradDeg = 0.0f;
	p.gradStrength = 0.05f;
	p.gradTarget = 0;
	p.gradShow = 0.0f;
	p.mixRule = 1;              // stochastic, the published code's default
	p.colour[0] = ofFloatColor(1.00f, 0.70f, 0.28f);   // amber
	p.colour[1] = ofFloatColor(0.36f, 0.80f, 1.00f);   // sky blue
	p.colour[2] = ofFloatColor(0.80f, 0.62f, 1.00f);   // lilac (no saturated red or green)
	p.gain = 1.6f;
	p.gammaExp = 0.8f;
	p.glow = 0.35f;
	p.glowMm = 8.0f;
	p.trailOn = true;
	p.trailSec = 0.5f;
	p.scaleBar = false;
	return p;
}
const LeniaLab::Params LeniaLab::DEFAULTS = makeDefaults();

static const char * PRESET_ROOT = "LENIALAB";
static const int HISTORY = 600;            // mass samples (every READOUT_EVERY frames)
static const int READOUT_EVERY = 10;       // frames
static const int REDUCE_BLOCK = 4;         // cells per block side

// ------------------------------------------------------------ GL helpers
static void allocateRect(ofFbo & fbo, int w, int h, GLint internalFormat)
{
	ofFboSettings s;
	s.width = std::max(1, w);
	s.height = std::max(1, h);
	s.internalformat = internalFormat;
	s.textureTarget = GL_TEXTURE_RECTANGLE_ARB;
	s.minFilter = GL_NEAREST;
	s.maxFilter = GL_NEAREST;
	s.wrapModeHorizontal = GL_CLAMP_TO_EDGE;
	s.wrapModeVertical = GL_CLAMP_TO_EDGE;
	s.useDepth = false;
	s.numSamples = 0;
	fbo.allocate(s);
	fbo.begin();
	ofClear(0, 0, 0, 0);
	fbo.end();
}

static void uploadRect(ofTexture & t, const std::vector<float> & rgba, int w, int h)
{
	if (!t.isAllocated() || (int)t.getWidth() != w || (int)t.getHeight() != h) {
		t.allocate(w, h, GL_RGBA32F, true);
		t.setTextureMinMagFilter(GL_NEAREST, GL_NEAREST);
		t.setTextureWrap(GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
	}
	t.loadData(rgba.data(), w, h, GL_RGBA);
}

static glm::mat4 mat(const float m[16]) { return glm::make_mat4(m); }

bool LeniaLab::loadShaders()
{
#ifdef TARGET_OPENGLES
	loadState = -1;
	return false;
#else
	const std::string dir = ofIsGLProgrammableRenderer() ? "shaders/lenia/GL3/" : "shaders/lenia/GL2/";
	const std::string v = dir + "lenia.vert";
	const bool ok = convShader.load(v, dir + "leniaConv.frag")
		&& flowShader.load(v, dir + "leniaFlow.frag")
		&& moveShader.load(v, dir + "leniaMove.frag")
		&& editShader.load(v, dir + "leniaEdit.frag")
		&& reduceShader.load(v, dir + "leniaReduce.frag")
		&& renderShader.load(v, dir + "leniaRender.frag")
		&& trailShader.load(v, dir + "leniaTrail.frag");
	if (!ok)
		ofLogError("LeniaLab") << "could not load the shaders in " << dir;
	loadState = ok ? 1 : -1;
	return ok;
#endif
}

// A quad over the whole target whose texcoords run 0..tw, 0..th (the
// pass's own texels), as DitherFilter's passes.
void LeniaLab::drawPass(ofFbo & target, ofShader & s, float tw, float th)
{
	(void)s;
	const float w = target.getWidth(), h = target.getHeight();
	quad.clear();
	quad.setMode(OF_PRIMITIVE_TRIANGLE_STRIP);
	quad.addVertex(glm::vec3(0, 0, 0)); quad.addTexCoord(glm::vec2(0, 0));
	quad.addVertex(glm::vec3(w, 0, 0)); quad.addTexCoord(glm::vec2(tw, 0));
	quad.addVertex(glm::vec3(0, h, 0)); quad.addTexCoord(glm::vec2(0, th));
	quad.addVertex(glm::vec3(w, h, 0)); quad.addTexCoord(glm::vec2(tw, th));
	quad.draw();
}

// ------------------------------------------------------------ setup
void LeniaLab::setup(int pw, int ph)
{
	projW = std::max(1, pw);
	projH = std::max(1, ph);
	std::string err;
	if (loadLibraryFile(ofToDataPath("lenia/species.json", true), lib, err))
		libStatus = ofToString(lib.species.size()) + " species, " + ofToString(lib.worlds.size()) + " worlds";
	else
		libStatus = "species.json: " + err;
	dither.setPresetFile("settings/leniaDither.xml");
	dither.params.enabled = false;      // off by default over the lab
	dither.params.classesOn = false;    // the lab's alpha carries no dot classes
	if (ofFile::doesFileExist("settings/leniaDither.xml"))
		dither.loadPreset();
	cfg = WorldConfig();
	cfg.model = MODEL_LENIA;
	if (lib.find("orbium") >= 0)
		cfg.channelSpecies[0] = "orbium";
	else
		for (const auto & s : lib.species)
			if (s.model == MODEL_LENIA) { cfg.channelSpecies[0] = s.id; break; }
	applyWorldConfig(false);
	if (ofFile::doesFileExist(presetFile))
		loadPreset();
}

void LeniaLab::setProjectorSize(int w, int h)
{
	projW = std::max(1, w);
	projH = std::max(1, h);
}

const Species * LeniaLab::channelSpecies(int c) const
{
	if (c < 0 || c >= MAX_CHANNELS) return nullptr;
	const int i = lib.find(cfg.channelSpecies[c]);
	return i >= 0 ? &lib.species[i] : nullptr;
}

bool LeniaLab::applyWorldConfig(bool keepDt)
{
	std::string err;
	WorldSetup w;
	if (!assembleWorld(lib, cfg, w, err)) { worldStatus = err; return false; }
	w.rScale = params.rScale;
	if (!finalizeWorld(w, err)) { worldStatus = err; return false; }
	setupW = w;
	if (!keepDt) params.dt = (float)setupW.dt;
	worldStatus = "";
	uploadTaps();
	return true;
}

bool LeniaLab::rebuildKernels()
{
	std::string err;
	setupW.rScale = params.rScale;
	if (!finalizeWorld(setupW, err)) { worldStatus = err; return false; }
	worldStatus = "";
	uploadTaps();
	return true;
}

void LeniaLab::uploadTaps()
{
	const TapTable & t = setupW.table;
	const int n = std::max(1, t.count);
	std::vector<float> rgba((size_t)n * 4 * 4, 0.0f);
	for (int i = 0; i < t.count; i++) {
		rgba[(0 * (size_t)n + i) * 4 + 0] = (float)t.dx[i];
		rgba[(0 * (size_t)n + i) * 4 + 1] = (float)t.dy[i];
		for (int k = 0; k < MAX_KERNELS; k++)
			rgba[((1 + k / 4) * (size_t)n + i) * 4 + k % 4] = t.w[(size_t)i * MAX_KERNELS + k];
	}
	uploadRect(tapTex, rgba, n, 4);
}

bool LeniaLab::ensureWorld()
{
	if (loadState == 0)
		loadShaders();
	if (loadState != 1)
		return false;
	const int W = std::max(16, (int)std::round(projW / (params.pxPerMm * params.cellMm)));
	const int H = std::max(16, (int)std::round(projH / (params.pxPerMm * params.cellMm)));
	if (W != worldWc || H != worldHc || !state[0].isAllocated()) {
		worldWc = W;
		worldHc = H;
		for (int i = 0; i < 2; i++) {
			allocateRect(state[i], W, H, GL_RGBA32F);
			allocateRect(param[i], W, H, GL_RGBA32F);
			allocateRect(disp[i], W, H, GL_RGBA32F);
			allocateRect(envBuf[i], W, H, GL_RGBA32F);
		}
		allocateRect(aff, W, H, GL_RGBA32F);
		allocateRect(reduceFbo, (W + REDUCE_BLOCK - 1) / REDUCE_BLOCK, (H + REDUCE_BLOCK - 1) / REDUCE_BLOCK, GL_RGBA32F);
		cur = 0;
		envCur = 0;
		prevValid = false;
		massResetPending = true;
		for (auto & h : massHistory) h.clear();
	}
	if (!outFbo.isAllocated() || (int)outFbo.getWidth() != projW || (int)outFbo.getHeight() != projH) {
		allocateRect(outFbo, projW, projH, GL_RGBA);
		for (int i = 0; i < 2; i++)
			allocateRect(trailFbo[i], projW, projH, GL_RGBA16F);
	}
	return true;
}

void LeniaLab::resetWorld()
{
	if (!state[0].isAllocated()) return;
	for (int i = 0; i < 2; i++) {
		state[i].begin(); ofClear(0, 0, 0, 0); state[i].end();
		param[i].begin(); ofClear(0, 0, 0, 0); param[i].end();
	}
	prevValid = false;
	steps = 0;
	simTime = 0.0;
	acc = 0.0;
	massResetPending = true;
	for (auto & h : massHistory) h.clear();
}

// ------------------------------------------------------------ passes
void LeniaLab::setEnvUniforms(ofShader & s)
{
	const Params & p = params;
	s.setUniform1i("maskMode", p.maskMode);
	s.setUniform2f("maskCentre", p.maskCentreX * worldWc, p.maskCentreY * worldHc);
	s.setUniform1f("maskRadius", mmToCells(p.maskRadiusMm));
	s.setUniform1f("maskEdge", std::max(1.0f, mmToCells(p.maskEdgeMm)));
	s.setUniform1f("outsideGrowth", p.outsideGrowth);
	s.setUniform1f("edgeRepel", p.edgeRepel);
	s.setUniform1i("gradMode", p.gradMode);
	const float a = ofDegToRad(p.gradDeg);
	s.setUniform2f("gradDir", std::cos(a), std::sin(a));
	s.setUniform1f("gradStrength", p.gradStrength);
	s.setUniform1i("gradTarget", p.gradTarget);
	s.setUniformTexture("envTex", envBuf[envCur].getTexture(), 3);
}

void LeniaLab::conv(ofFbo & out, int mode)
{
	const WorldSetup & w = setupW;
	out.begin();
	convShader.begin();
	convShader.setUniformTexture("stateTex", state[cur].getTexture(), 0);
	convShader.setUniformTexture("tapTex", tapTex, 1);
	convShader.setUniformTexture("paramTex", param[cur].getTexture(), 2);
	setEnvUniforms(convShader);
	convShader.setUniform2f("worldSize", (float)worldWc, (float)worldHc);
	convShader.setUniform1i("wrapWorld", params.wrapWorld ? 1 : 0);
	convShader.setUniform1i("nTaps", w.table.count);
	convShader.setUniform1i("nKernels", (int)w.kernels.size());
	for (int g = 0; g < 3; g++) {
		convShader.setUniformMatrix4f("srcSel" + ofToString(g), mat(w.srcSel[g]));
		convShader.setUniformMatrix4f("dstSel" + ofToString(g), mat(w.dstSel[g]));
		convShader.setUniformMatrix4f("slotSel" + ofToString(g), mat(w.slotSel[g]));
	}
	convShader.setUniform4fv("kM", w.m, 3);
	convShader.setUniform4fv("kS", w.s, 3);
	convShader.setUniform4fv("kH", w.h, 3);
	convShader.setUniform4fv("kGn", w.gn, 3);
	convShader.setUniform4fv("kRel", w.rel, 3);
	convShader.setUniform1i("mode", mode);
	convShader.setUniform1i("embed", w.embed ? 1 : 0);
	convShader.setUniform1f("dt", params.dt);
	convShader.setUniform3f("channelOn", w.channelOn[0], w.channelOn[1], w.channelOn[2]);
	drawPass(out, convShader, (float)worldWc, (float)worldHc);
	convShader.end();
	out.end();
}

void LeniaLab::step()
{
	const WorldSetup & w = setupW;
	const int nxt = 1 - cur;
	if (w.model == MODEL_LENIA) {
		conv(state[nxt], 0);
	} else {
		conv(aff, 1);
		for (int pair = 0; pair < 2; pair++) {
			if (pair == 1 && w.channelOn[2] == 0.0f) continue;
			disp[pair].begin();
			flowShader.begin();
			flowShader.setUniformTexture("stateTex", state[cur].getTexture(), 0);
			flowShader.setUniformTexture("affTex", aff.getTexture(), 1);
			setEnvUniforms(flowShader);
			flowShader.setUniform2f("worldSize", (float)worldWc, (float)worldHc);
			flowShader.setUniform1i("wrapWorld", params.wrapWorld ? 1 : 0);
			flowShader.setUniform1i("pair", pair);
			flowShader.setUniform1f("dt", params.dt);
			flowShader.setUniform1f("dd", (float)w.dd);
			flowShader.setUniform1f("sigma", (float)w.sigma);
			flowShader.setUniform1f("theta", (float)w.theta);
			drawPass(disp[pair], flowShader, (float)worldWc, (float)worldHc);
			flowShader.end();
			disp[pair].end();
		}
		// P first (it reads the old A), then A
		for (int target = w.embed ? 1 : 0; target >= 0; target--) {
			ofFbo & out = target == 1 ? param[nxt] : state[nxt];
			out.begin();
			moveShader.begin();
			moveShader.setUniformTexture("stateTex", state[cur].getTexture(), 0);
			moveShader.setUniformTexture("dispTex0", disp[0].getTexture(), 1);
			moveShader.setUniformTexture("dispTex1", disp[1].getTexture(), 2);
			moveShader.setUniformTexture("paramTex", param[cur].getTexture(), 3);
			moveShader.setUniform2f("worldSize", (float)worldWc, (float)worldHc);
			moveShader.setUniform1i("wrapWorld", params.wrapWorld ? 1 : 0);
			moveShader.setUniform1i("dd", std::max(1, std::min(7, w.dd)));
			moveShader.setUniform1f("sigma", (float)w.sigma);
			moveShader.setUniform1i("target", target);
			moveShader.setUniform1i("mixRule", params.mixRule);
			moveShader.setUniform1f("stepIndex", (float)(steps % 4096));
			drawPass(out, moveShader, (float)worldWc, (float)worldHc);
			moveShader.end();
			out.end();
		}
	}
	if (!w.embed) {
		// P is unused: keep the next buffer equal (cleared)
	}
	cur = nxt;
	steps++;
	simTime += params.dt;
	prevValid = true;
}

// op 0 stamp (stampTex already uploaded), 1 add, 2 erase, 3 paint env
void LeniaLab::edit(int op, const ofVec2f & centre, const Grid * stampGrids, int nStamp, int stampBase, int slot)
{
	const bool embed = setupW.embed;
	ofVec2f origin(0, 0), size(0, 0);
	if (op == 0) {
		int n = 0;
		for (int i = 0; i < nStamp; i++) n = std::max(n, std::max(stampGrids[i].w, stampGrids[i].h));
		if (n == 0) return;
		std::vector<float> rgba((size_t)n * n * 4, 0.0f);
		for (int i = 0; i < nStamp; i++) {
			const Grid & g = stampGrids[i];
			const int ch = embed ? 0 : std::min(2, stampBase + i);
			for (int y = 0; y < g.h; y++)
				for (int x = 0; x < g.w; x++)
					rgba[((size_t)y * n + x) * 4 + ch] += g.v[(size_t)y * g.w + x];
		}
		uploadRect(stampTex, rgba, n, n);
		origin.set(std::round(centre.x - n * 0.5f), std::round(centre.y - n * 0.5f));
		size.set((float)n, (float)n);
	}
	const int nxt = 1 - cur;
	auto run = [&](ofFbo & out, ofFbo & src, int target) {
		out.begin();
		editShader.begin();
		editShader.setUniformTexture("stateTex", src.getTexture(), 0);
		if (stampTex.isAllocated())
			editShader.setUniformTexture("stampTex", stampTex, 1);
		editShader.setUniformTexture("paramTex", param[cur].getTexture(), 2);
		editShader.setUniform2f("worldSize", (float)worldWc, (float)worldHc);
		editShader.setUniform1i("op", op);
		editShader.setUniform1i("target", target);
		editShader.setUniform2f("stampOrigin", origin.x, origin.y);
		editShader.setUniform2f("stampSize", size.x, size.y);
		editShader.setUniform3f("stampChannels", 1, 1, 1);
		editShader.setUniform2f("brushCentre", centre.x, centre.y);
		editShader.setUniform1f("brushRadius", mmToCells(params.brushMm));
		editShader.setUniform1f("brushAmount", params.brushAmount);
		const int ch = embed ? 0 : std::max(0, std::min(2, params.spawnChannel));
		editShader.setUniform3f("brushChannels", ch == 0 ? 1.f : 0.f, ch == 1 ? 1.f : 0.f, ch == 2 ? 1.f : 0.f);
		const bool mask = params.tool == TOOL_MASK_PAINT || params.tool == TOOL_MASK_ERASE;
		const bool paint = params.tool == TOOL_MASK_PAINT || params.tool == TOOL_GRAD_PAINT;
		editShader.setUniform1i("envChannel", mask ? 0 : 1);
		editShader.setUniform1f("envValue", paint ? 1.0f : 0.0f);
		editShader.setUniform1i("clampOne", setupW.model == MODEL_LENIA ? 1 : 0);
		editShader.setUniform4f("slotVec", slot == 0 ? 1.f : 0.f, slot == 1 ? 1.f : 0.f, slot == 2 ? 1.f : 0.f, 0.f);
		drawPass(out, editShader, (float)worldWc, (float)worldHc);
		editShader.end();
		out.end();
	};
	if (op == 3) {
		run(envBuf[1 - envCur], envBuf[envCur], 0);
		envCur = 1 - envCur;
		return;
	}
	if (embed)
		run(param[nxt], state[cur], 1);
	run(state[nxt], state[cur], 0);
	if (!embed) {
		param[nxt].begin(); ofClear(0, 0, 0, 0); param[nxt].end();
	}
	cur = nxt;
	prevValid = false;
	massResetPending = true;    // the mass drift readout counts from the last edit
}

void LeniaLab::stampSpecies(int channel, const ofVec2f & cell, float deg)
{
	const Species * sp = channelSpecies(channel);
	if (!sp) return;
	const int base = setupW.embed ? 0 : channel;
	std::vector<Grid> grids;
	if (!sp->cells.empty()) {
		for (const Grid & g : sp->cells)
			grids.push_back(transformGrid(g, params.rScale, deg));
	} else {
		const int side = std::max(4, (int)std::round((sp->noisePatch > 0 ? sp->noisePatch : 20) * params.rScale));
		for (int c = 0; c < sp->channels; c++) {
			Grid g;
			g.w = g.h = side;
			g.v.resize((size_t)side * side);
			for (auto & v : g.v) v = ofRandom(1.0f);
			grids.push_back(g);
		}
	}
	edit(0, cell, grids.data(), (int)grids.size(), base, channel);
}

void LeniaLab::scatter()
{
	for (int i = 0; i < params.scatterCount; i++) {
		int c = params.spawnChannel;
		if (!setupW.embed) {
			// any channel with a species
			std::vector<int> chs;
			for (int k = 0; k < MAX_CHANNELS; k++) if (channelSpecies(k)) chs.push_back(k);
			if (chs.empty()) return;
			c = chs[(int)ofRandom(chs.size()) % chs.size()];
		} else {
			std::vector<int> slots;
			for (int k = 0; k < MAX_CHANNELS; k++) if (channelSpecies(k)) slots.push_back(k);
			if (slots.empty()) return;
			c = slots[(int)ofRandom(slots.size()) % slots.size()];
		}
		stampSpecies(c, ofVec2f(ofRandom(0.1f, 0.9f) * worldWc, ofRandom(0.1f, 0.9f) * worldHc), ofRandom(360.0f));
	}
}

void LeniaLab::noisePatches()
{
	const int side = std::max(4, (int)std::round(mmToCells(params.noiseMm)));
	const int ch = setupW.embed ? 0 : std::max(0, std::min(2, params.spawnChannel));
	for (int i = 0; i < params.scatterCount; i++) {
		Grid g;
		g.w = g.h = side;
		g.v.resize((size_t)side * side);
		for (auto & v : g.v) v = ofRandom(1.0f);
		edit(0, ofVec2f(ofRandom(0.1f, 0.9f) * worldWc, ofRandom(0.1f, 0.9f) * worldHc), &g, 1, ch, params.spawnChannel);
	}
}

// Block sums -> total mass per channel and a creature count (8-connected
// blocks whose mean over the channels is above 0.05, at least 2 blocks).
void LeniaLab::reduceAndCount()
{
	const int bw = (int)reduceFbo.getWidth(), bh = (int)reduceFbo.getHeight();
	reduceFbo.begin();
	reduceShader.begin();
	reduceShader.setUniformTexture("stateTex", state[cur].getTexture(), 0);
	reduceShader.setUniform2f("worldSize", (float)worldWc, (float)worldHc);
	reduceShader.setUniform1f("block", (float)REDUCE_BLOCK);
	drawPass(reduceFbo, reduceShader, (float)bw, (float)bh);
	reduceShader.end();
	reduceFbo.end();
	ofFloatPixels px;
	reduceFbo.readToPixels(px);
	if ((int)px.getWidth() != bw || (int)px.getHeight() != bh) return;
	double m[3] = { 0, 0, 0 };
	std::vector<unsigned char> on((size_t)bw * bh, 0);
	const float * d = px.getData();
	const int nc = (int)px.getNumChannels();
	if (nc < 4) return;
	for (int i = 0; i < bw * bh; i++) {
		const float * t = d + (size_t)i * nc;
		m[0] += t[0]; m[1] += t[1]; m[2] += t[2];
		const float n = std::max(1.0f, t[3]);
		on[i] = (t[0] + t[1] + t[2]) / n > 0.05f ? 1 : 0;
	}
	int count = 0;
	std::vector<int> stack;
	for (int i = 0; i < bw * bh; i++) {
		if (on[i] != 1) continue;
		int size = 0;
		stack.push_back(i);
		on[i] = 2;
		while (!stack.empty()) {
			const int j = stack.back();
			stack.pop_back();
			size++;
			const int x = j % bw, y = j / bw;
			for (int dy = -1; dy <= 1; dy++)
				for (int dx = -1; dx <= 1; dx++) {
					int xx = x + dx, yy = y + dy;
					if (params.wrapWorld) { xx = (xx + bw) % bw; yy = (yy + bh) % bh; }
					if (xx < 0 || yy < 0 || xx >= bw || yy >= bh) continue;
					const int k = yy * bw + xx;
					if (on[k] == 1) { on[k] = 2; stack.push_back(k); }
				}
		}
		if (size >= 2) count++;
	}
	creatures = count;
	for (int c = 0; c < 3; c++) {
		mass[c] = (float)m[c];
		if (massResetPending) massAtReset[c] = mass[c];
		massHistory[c].push_back(mass[c]);
		while ((int)massHistory[c].size() > HISTORY) massHistory[c].pop_front();
	}
	massResetPending = false;
}

void LeniaLab::render(float frac)
{
	const Params & p = params;
	outFbo.begin();
	ofClear(0, 0, 0, 255);
	renderShader.begin();
	renderShader.setUniformTexture("stateTex", state[cur].getTexture(), 0);
	renderShader.setUniformTexture("prevTex", (prevValid ? state[1 - cur] : state[cur]).getTexture(), 1);
	renderShader.setUniformTexture("paramTex", param[cur].getTexture(), 2);
	setEnvUniforms(renderShader);
	renderShader.setUniform2f("worldSize", (float)worldWc, (float)worldHc);
	renderShader.setUniform2f("outSize", (float)projW, (float)projH);
	renderShader.setUniform1f("frac", prevValid ? frac : 1.0f);
	renderShader.setUniform1i("embed", setupW.embed ? 1 : 0);
	renderShader.setUniform3f("colour0", p.colour[0].r, p.colour[0].g, p.colour[0].b);
	renderShader.setUniform3f("colour1", p.colour[1].r, p.colour[1].g, p.colour[1].b);
	renderShader.setUniform3f("colour2", p.colour[2].r, p.colour[2].g, p.colour[2].b);
	renderShader.setUniform3f("channelVisible", 1, 1, 1);
	renderShader.setUniform1f("gain", p.gain);
	renderShader.setUniform1f("gammaExp", p.gammaExp);
	renderShader.setUniform1f("glowAmount", p.glow);
	renderShader.setUniform1f("glowRadius", mmToCells(p.glowMm));
	renderShader.setUniform1f("maskShow", p.maskShow);
	renderShader.setUniform1f("gradShow", p.gradShow);
	drawPass(outFbo, renderShader, (float)projW, (float)projH);
	renderShader.end();
	outFbo.end();
	if (p.trailOn) {
		const int nt = 1 - trailCur;
		trailFbo[nt].begin();
		trailShader.begin();
		trailShader.setUniformTexture("colourTex", outFbo.getTexture(), 0);
		trailShader.setUniformTexture("trailTex", trailFbo[trailCur].getTexture(), 1);
		trailShader.setUniform1f("decay", std::exp(-(float)ofGetLastFrameTime() / std::max(0.02f, p.trailSec)));
		drawPass(trailFbo[nt], trailShader, (float)projW, (float)projH);
		trailShader.end();
		trailFbo[nt].end();
		trailCur = nt;
	}
}

// ------------------------------------------------------------ frame
void LeniaLab::update(double frameSec)
{
	if (!ensureWorld()) return;
	ofPushStyle();
	ofEnableBlendMode(OF_BLENDMODE_DISABLED);
	timer.begin();
	int n = 0;
	if (!params.paused && params.dt > 0) {
		acc += frameSec * params.timeScale / params.dt;
		n = (int)std::floor(acc);
		if (n > params.maxStepsPerFrame) {
			n = params.maxStepsPerFrame;
			acc = 0.0;      // falling behind: drop time rather than queue it
		} else {
			acc -= n;
		}
	}
	for (int i = 0; i < n; i++)
		step();
	stepsPerSec = frameSec > 0 ? 0.9f * stepsPerSec + 0.1f * (float)(n / frameSec) : stepsPerSec;
	render((float)acc);
	if (frameCount++ % READOUT_EVERY == 0)
		reduceAndCount();
	timer.end();
	ofPopStyle();
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
}

void LeniaLab::drawOutput(float x, float y, float w, float h)
{
	if (!outFbo.isAllocated()) return;
	ofPushStyle();
	ofEnableBlendMode(OF_BLENDMODE_DISABLED);
	ofSetColor(255);
	(params.trailOn ? trailFbo[trailCur] : outFbo).draw(x, y, w, h);
	if (params.scaleBar) {
		// 100 mm on the sand, bottom left
		const float sx = w / projW, sy = h / projH;
		const float len = 100.0f * params.pxPerMm * sx;
		ofSetColor(200);
		ofDrawRectangle(x + 40 * sx, y + h - 40 * sy, len, std::max(1.0f, 4 * sy));
		ofDrawRectangle(x + 40 * sx, y + h - 52 * sy, std::max(1.0f, 2 * sx), 16 * sy);
		ofDrawRectangle(x + 40 * sx + len, y + h - 52 * sy, std::max(1.0f, 2 * sx), 16 * sy);
	}
	ofPopStyle();
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
}

ofRectangle LeniaLab::previewRectFor(const ofRectangle & area) const
{
	const float a = (float)projW / projH;
	float w = area.width, h = w / a;
	if (h > area.height) { h = area.height; w = h * a; }
	return ofRectangle(area.x + (area.width - w) * 0.5f, area.y + (area.height - h) * 0.5f, w, h);
}

// ------------------------------------------------------------ mouse
ofVec2f LeniaLab::windowToCell(float x, float y) const
{
	if (previewRect.width <= 0 || previewRect.height <= 0) return ofVec2f(-1, -1);
	return ofVec2f((x - previewRect.x) / previewRect.width * worldWc, (y - previewRect.y) / previewRect.height * worldHc);
}

void LeniaLab::applyTool(const ofVec2f & cell, bool first)
{
	if (loadState != 1 || !state[0].isAllocated()) return;
	if (cell.x < 0 || cell.y < 0 || cell.x >= worldWc || cell.y >= worldHc) return;
	switch (params.tool) {
	case TOOL_SPAWN:
		if (first)
			stampSpecies(params.spawnChannel, cell, params.spawnRandomRot ? ofRandom(360.0f) : params.spawnDeg);
		break;
	case TOOL_ADD: edit(1, cell, nullptr, 0, 0, params.spawnChannel); break;
	case TOOL_ERASE: edit(2, cell, nullptr, 0, 0, params.spawnChannel); break;
	default: edit(3, cell, nullptr, 0, 0, 0); break;
	}
}

void LeniaLab::mousePressed(float x, float y, int button)
{
	if (button != OF_MOUSE_BUTTON_LEFT) return;
	if (!previewRect.inside(x, y)) return;
	dragging = true;
	lastCell = windowToCell(x, y);
	applyTool(lastCell, true);
}

void LeniaLab::mouseDragged(float x, float y, int button)
{
	if (!dragging || button != OF_MOUSE_BUTTON_LEFT) return;
	const ofVec2f c = windowToCell(x, y);
	// brush strokes: one dab per half brush radius moved
	if (c.distance(lastCell) < std::max(1.0f, 0.5f * mmToCells(params.brushMm))) return;
	lastCell = c;
	applyTool(c, false);
}

void LeniaLab::mouseReleased(float x, float y, int button)
{
	(void)x; (void)y; (void)button;
	dragging = false;
}

void LeniaLab::keyPressed(int key)
{
	if (key == '[') params.spawnDeg = std::fmod(params.spawnDeg + 315.0f, 360.0f);
	else if (key == ']') params.spawnDeg = std::fmod(params.spawnDeg + 45.0f, 360.0f);
}

// ------------------------------------------------------------ timer
void LeniaLab::GpuTimer::begin()
{
	started = false;
	if (!checked) {
		checked = true;
		supported = glewIsSupported("GL_ARB_timer_query") || glewIsSupported("GL_VERSION_3_3");
		if (supported)
			glGenQueries(2, ids);
	}
	if (!supported)
		return;
	const int other = 1 - cur;
	if (pending[other]) {
		GLint available = 0;
		glGetQueryObjectiv(ids[other], GL_QUERY_RESULT_AVAILABLE, &available);
		if (available) {
			GLuint64 ns = 0;
			glGetQueryObjectui64v(ids[other], GL_QUERY_RESULT, &ns);
			lastMs = (float)(ns / 1.0e6);
			pending[other] = false;
		}
	}
	if (pending[cur])
		return;
	glBeginQuery(GL_TIME_ELAPSED, ids[cur]);
	started = true;
}

void LeniaLab::GpuTimer::end()
{
	if (!started)
		return;
	glEndQuery(GL_TIME_ELAPSED);
	pending[cur] = true;
	cur = 1 - cur;
	started = false;
}

// ------------------------------------------------------------ preset
static std::string kernelToString(const Kernel & k)
{
	std::string b;
	for (size_t i = 0; i < k.b.size(); i++) b += (i ? "," : "") + ofToString(k.b[i], 6);
	return ofToString(k.from) + ";" + ofToString(k.to) + ";" + ofToString(k.shape) + ";" + ofToString(k.R, 6) + ";"
		+ ofToString(k.r, 6) + ";" + ofToString(k.kn) + ";" + b + ";"
		+ ofToString(k.ra[0], 6) + "," + ofToString(k.ra[1], 6) + "," + ofToString(k.ra[2], 6) + ";"
		+ ofToString(k.rb[0], 6) + "," + ofToString(k.rb[1], 6) + "," + ofToString(k.rb[2], 6) + ";"
		+ ofToString(k.rw[0], 6) + "," + ofToString(k.rw[1], 6) + "," + ofToString(k.rw[2], 6) + ";"
		+ ofToString(k.m, 6) + ";" + ofToString(k.s, 6) + ";" + ofToString(k.h, 6) + ";" + ofToString(k.gn) + ";" + (k.relative ? "1" : "0");
}

static bool kernelFromString(const std::string & s, Kernel & k)
{
	const auto f = ofSplitString(s, ";");
	if (f.size() != 15) return false;
	k.from = ofToInt(f[0]); k.to = ofToInt(f[1]); k.shape = ofToInt(f[2]); k.R = ofToDouble(f[3]);
	k.r = ofToDouble(f[4]); k.kn = ofToInt(f[5]);
	k.b.clear();
	for (const auto & x : ofSplitString(f[6], ",", true, true)) k.b.push_back(ofToDouble(x));
	if (k.b.empty()) k.b.push_back(1.0);
	auto trip = [](const std::string & t, double o[3]) {
		const auto v = ofSplitString(t, ",");
		for (int i = 0; i < 3 && i < (int)v.size(); i++) o[i] = ofToDouble(v[i]);
	};
	trip(f[7], k.ra); trip(f[8], k.rb); trip(f[9], k.rw);
	k.m = ofToDouble(f[10]); k.s = ofToDouble(f[11]); k.h = ofToDouble(f[12]); k.gn = ofToInt(f[13]); k.relative = f[14] == "1";
	return k.from >= 0 && k.from < MAX_CHANNELS && k.to >= 0 && k.to < MAX_CHANNELS && k.s > 0;
}

bool LeniaLab::savePreset()
{
	const Params & p = params;
	ofXml xml;
	auto s = xml.appendChild(PRESET_ROOT);
	s.appendChild("model").set(cfg.model);
	s.appendChild("embed").set(cfg.embed ? 1 : 0);
	for (int c = 0; c < MAX_CHANNELS; c++)
		s.appendChild("species" + ofToString(c)).set(cfg.channelSpecies[c]);
	auto ks = s.appendChild("kernels");
	for (const auto & k : setupW.kernels)
		ks.appendChild("k").set(kernelToString(k));
	auto xk = s.appendChild("crossKernels");
	for (const auto & k : cfg.crossKernels)
		xk.appendChild("k").set(kernelToString(k));
#define SAVE(N) s.appendChild(#N).set(p.N);
#define SAVEB(N) s.appendChild(#N).set(p.N ? 1 : 0);
	SAVE(pxPerMm) SAVE(cellMm) SAVE(rScale) SAVE(timeScale) SAVE(dt) SAVEB(wrapWorld) SAVE(maxStepsPerFrame)
	SAVE(spawnChannel) SAVE(spawnDeg) SAVEB(spawnRandomRot) SAVE(brushMm) SAVE(brushAmount) SAVE(scatterCount) SAVE(noiseMm)
	SAVE(maskMode) SAVE(maskRadiusMm) SAVE(maskEdgeMm) SAVE(maskCentreX) SAVE(maskCentreY) SAVE(outsideGrowth) SAVE(edgeRepel) SAVE(maskShow)
	SAVE(gradMode) SAVE(gradDeg) SAVE(gradStrength) SAVE(gradTarget) SAVE(gradShow) SAVE(mixRule)
	SAVE(gain) SAVE(gammaExp) SAVE(glow) SAVE(glowMm) SAVEB(trailOn) SAVE(trailSec) SAVEB(scaleBar)
#undef SAVE
#undef SAVEB
	for (int i = 0; i < 3; i++)
		s.appendChild("colour" + ofToString(i)).set(ofToString(p.colour[i].r) + "," + ofToString(p.colour[i].g) + "," + ofToString(p.colour[i].b));
	const bool ok = xml.save(presetFile) && dither.savePreset();
	presetStatus = ok ? "saved " + presetFile : "save failed";
	return ok;
}

bool LeniaLab::loadPreset()
{
	ofXml xml;
	if (!xml.load(presetFile)) { presetStatus = "no preset file"; return false; }
	auto s = xml.getChild(PRESET_ROOT);
	if (!s) { presetStatus = "preset file not recognised"; return false; }
	Params & p = params;
#define LOAD_I(N) if (s.getChild(#N)) p.N = s.getChild(#N).getIntValue();
#define LOAD_F(N) if (s.getChild(#N)) p.N = s.getChild(#N).getFloatValue();
#define LOAD_B(N) if (s.getChild(#N)) p.N = s.getChild(#N).getIntValue() != 0;
	LOAD_F(pxPerMm) LOAD_F(cellMm) LOAD_F(rScale) LOAD_F(timeScale) LOAD_B(wrapWorld) LOAD_I(maxStepsPerFrame)
	LOAD_I(spawnChannel) LOAD_F(spawnDeg) LOAD_B(spawnRandomRot) LOAD_F(brushMm) LOAD_F(brushAmount) LOAD_I(scatterCount) LOAD_F(noiseMm)
	LOAD_I(maskMode) LOAD_F(maskRadiusMm) LOAD_F(maskEdgeMm) LOAD_F(maskCentreX) LOAD_F(maskCentreY) LOAD_F(outsideGrowth) LOAD_F(edgeRepel) LOAD_F(maskShow)
	LOAD_I(gradMode) LOAD_F(gradDeg) LOAD_F(gradStrength) LOAD_I(gradTarget) LOAD_F(gradShow) LOAD_I(mixRule)
	LOAD_F(gain) LOAD_F(gammaExp) LOAD_F(glow) LOAD_F(glowMm) LOAD_B(trailOn) LOAD_F(trailSec) LOAD_B(scaleBar)
#undef LOAD_I
#undef LOAD_F
#undef LOAD_B
	for (int i = 0; i < 3; i++) {
		auto c = s.getChild("colour" + ofToString(i));
		if (!c) continue;
		const auto v = ofSplitString(c.getValue(), ",", true, true);
		if (v.size() == 3) p.colour[i] = ofFloatColor(ofToFloat(v[0]), ofToFloat(v[1]), ofToFloat(v[2]));
	}
	p.cellMm = ofClamp(p.cellMm, 0.5f, 8.0f);
	p.pxPerMm = ofClamp(p.pxPerMm, 0.2f, 10.0f);
	p.rScale = ofClamp(p.rScale, 0.25f, 2.0f);
	WorldConfig c;
	if (s.getChild("model")) c.model = s.getChild("model").getIntValue() == MODEL_FLOW ? MODEL_FLOW : MODEL_LENIA;
	if (s.getChild("embed")) c.embed = s.getChild("embed").getIntValue() != 0;
	for (int i = 0; i < MAX_CHANNELS; i++)
		if (s.getChild("species" + ofToString(i))) c.channelSpecies[i] = s.getChild("species" + ofToString(i)).getValue();
	if (auto xk = s.getChild("crossKernels"))
		for (auto k : xk.getChildren("k")) {
			Kernel K;
			if (kernelFromString(k.getValue(), K)) c.crossKernels.push_back(K);
		}
	const WorldConfig old = cfg;
	cfg = c;
	if (!applyWorldConfig(true)) {
		presetStatus = "preset world refused: " + worldStatus;
		cfg = old;
		applyWorldConfig(true);
		return false;
	}
	// saved kernel values (GUI edits), if they still match the world
	if (auto ks = s.getChild("kernels")) {
		std::vector<Kernel> saved;
		for (auto k : ks.getChildren("k")) {
			Kernel K;
			if (kernelFromString(k.getValue(), K)) saved.push_back(K);
		}
		if (saved.size() == setupW.kernels.size()) {
			setupW.kernels = saved;
			rebuildKernels();
		}
	}
	if (s.getChild("dt")) p.dt = s.getChild("dt").getFloatValue();
	dither.loadPreset();
	resetWorld();
	presetStatus = "loaded " + presetFile;
	return true;
}

// ------------------------------------------------------------ GUI
static const char * statusTag(const std::string & s)
{
	if (s == "published") return "published";
	if (s == "sampled") return "sampled (published code, seeded)";
	return "TUNED (not published)";
}

void LeniaLab::setMode(int model)
{
	cfg = WorldConfig();
	cfg.model = model;
	for (const auto & s : lib.species)
		if (s.model == model) { cfg.channelSpecies[0] = s.id; break; }
	applyWorldConfig(false);
	resetWorld();
}

void LeniaLab::drawKernelGui()
{
	bool changed = false;
	int remove = -1;
	static const char * gns[] = { "polynomial (1)", "Gaussian (2)", "step (3)" };
	static const char * kns[] = { "polynomial (1)", "exponential (2)", "step (3)" };
	for (size_t i = 0; i < setupW.kernels.size(); i++) {
		Kernel & k = setupW.kernels[i];
		const int owner = i < setupW.kernelOwner.size() ? setupW.kernelOwner[i] : -1;
		ImGui::PushID((int)i);
		std::string title = "Kernel " + ofToString(i) + ": ch " + ofToString(k.from) + " -> ch " + ofToString(k.to)
			+ (owner < 0 ? "  (cross)" : setupW.embed ? "  (slot " + ofToString(owner) + ")" : "");
		if (ImGui::TreeNode(title.c_str())) {
			float m = (float)k.m, s = (float)k.s, h = (float)k.h, r = (float)k.r;
			if (ImGui::SliderFloat("mu (growth centre)", &m, 0.0f, 1.0f, "%.4f")) { k.m = m; changed = true; }
			if (ImGui::SliderFloat("sigma (growth width)", &s, 0.001f, 0.3f, "%.4f")) { k.s = std::max(1e-4f, s); changed = true; }
			if (ImGui::SliderFloat("h (weight)", &h, 0.0f, 2.0f, "%.3f")) { k.h = h; changed = true; }
			int gn = k.gn - 1;
			if (ImGui::Combo("growth", &gn, gns, 3)) { k.gn = gn + 1; changed = true; }
			if (ImGui::Checkbox("relative (lab extension: h (G(U) - G(0)))", &k.relative)) changed = true;
			ImGui::Text("R %.1f cells x R scale %.2f = %.1f mm", k.R, params.rScale, k.R * params.rScale * params.cellMm);
			if (ImGui::SliderFloat("r (relative radius)", &r, 0.1f, 1.0f, "%.3f")) { k.r = r; changed = true; }
			if (k.shape == SHAPE_SHELLS) {
				int kn = k.kn - 1;
				if (ImGui::Combo("core", &kn, kns, 3)) { k.kn = kn + 1; changed = true; }
				for (size_t j = 0; j < k.b.size(); j++) {
					float b = (float)k.b[j];
					ImGui::PushID((int)j);
					if (ImGui::SliderFloat("shell weight b", &b, 0.0f, 1.0f, "%.3f")) { k.b[j] = b; changed = true; }
					ImGui::PopID();
				}
				if (k.b.size() < 4 && ImGui::Button("Add shell")) { k.b.push_back(1.0); changed = true; }
				if (k.b.size() > 1) { ImGui::SameLine(); if (ImGui::Button("Remove shell")) { k.b.pop_back(); changed = true; } }
			} else {
				for (int j = 0; j < 3; j++) {
					float a = (float)k.ra[j], b = (float)k.rb[j], w = (float)k.rw[j];
					ImGui::PushID(j);
					ImGui::Text("ring %d", j);
					if (ImGui::SliderFloat("a (position)", &a, 0.0f, 1.0f)) { k.ra[j] = a; changed = true; }
					if (ImGui::SliderFloat("b (height)", &b, 0.0f, 1.0f)) { k.rb[j] = b; changed = true; }
					if (ImGui::SliderFloat("w (width)", &w, 0.01f, 0.5f)) { k.rw[j] = w; changed = true; }
					ImGui::PopID();
				}
			}
			if (owner < 0 && ImGui::Button("Remove this cross kernel")) remove = (int)i;
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
	if (remove >= 0) {
		// cross kernels live in cfg as well (world channel indices)
		int crossIndex = -1;
		for (int i = 0; i <= remove; i++) if (setupW.kernelOwner[i] < 0) crossIndex++;
		if (crossIndex >= 0 && crossIndex < (int)cfg.crossKernels.size())
			cfg.crossKernels.erase(cfg.crossKernels.begin() + crossIndex);
		applyWorldConfig(true);
		return;
	}
	if (changed) {
		// keep cfg's cross kernels in step with the edited ones
		int ci = 0;
		for (size_t i = 0; i < setupW.kernels.size(); i++)
			if (setupW.kernelOwner[i] < 0 && ci < (int)cfg.crossKernels.size())
				cfg.crossKernels[ci++] = setupW.kernels[i];
		rebuildKernels();
	}
	if (!setupW.embed) {
		static int xf = 1, xt = 0;
		ImGui::SliderInt("cross from ch", &xf, 0, 2);
		ImGui::SliderInt("cross to ch", &xt, 0, 2);
		if (ImGui::Button("Add cross kernel (relative, Gaussian m 0, s 0.02, h 0.5)")) {
			Kernel k;
			k.from = xf; k.to = xt; k.shape = SHAPE_SHELLS; k.b = { 1.0 }; k.kn = CORE_POLY;
			k.m = 0.0; k.s = 0.02; k.h = 0.5; k.gn = GROWTH_GAUSS; k.relative = true; k.R = -1;
			cfg.crossKernels.push_back(k);
			applyWorldConfig(true);
		}
	}
}

void LeniaLab::drawGui()
{
	Params & p = params;
	ImGui::Begin("Lenia lab");
	ImGui::PushID("LeniaLab");
	ImGui::TextWrapped("Lenia organisms alone on black (no terrain, water, succession or plants; those are paused). "
		"Mouse over the preview stands in for a hand.");
	if (loadState == -1)
		ImGui::Text("Shaders not loaded (bin/data/shaders/lenia): nothing is drawn.");
	ImGui::Text("Library: %s", libStatus.c_str());
	ImGui::Text("World %d x %d cells = %.0f x %.0f mm; %.1f steps/s; t = %.1f; GPU %.2f ms/frame",
		worldWc, worldHc, worldWc * p.cellMm, worldHc * p.cellMm, stepsPerSec, simTime, timer.ms());
	if (ImGui::Button("Save preset")) savePreset();
	ImGui::SameLine();
	if (ImGui::Button("Load preset")) loadPreset();
	ImGui::SameLine();
	if (ImGui::Button("Defaults")) { p = DEFAULTS; applyWorldConfig(false); resetWorld(); }
	if (!presetStatus.empty()) ImGui::Text("Preset: %s", presetStatus.c_str());
	if (!worldStatus.empty()) ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%s", worldStatus.c_str());

	// --- model and species
	if (ImGui::CollapsingHeader("Model and species", ImGuiTreeNodeFlags_DefaultOpen)) {
		int model = cfg.model;
		if (ImGui::RadioButton("Lenia", &model, MODEL_LENIA) && model != cfg.model) setMode(MODEL_LENIA);
		ImGui::SameLine();
		if (ImGui::RadioButton("Flow-Lenia (mass conserved)", &model, MODEL_FLOW) && model != cfg.model) setMode(MODEL_FLOW);
		if (cfg.model == MODEL_FLOW) {
			bool embed = cfg.embed;
			if (ImGui::Checkbox("Parameter embedding (species share matter; P travels with it)", &embed)) {
				cfg.embed = embed;
				cfg.crossKernels.clear();
				if (!applyWorldConfig(true)) { cfg.embed = !embed; applyWorldConfig(true); }
				resetWorld();
			}
			if (cfg.embed) {
				static const char * mixes[] = { "average (mass-weighted)", "stochastic (one source)" };
				ImGui::Combo("Mixing at collisions", &p.mixRule, mixes, 2);
			}
		}
		std::vector<std::string> ids = { "" };
		std::vector<std::string> names = { "(none)" };
		for (const auto & s : lib.species)
			if (s.model == cfg.model) { ids.push_back(s.id); names.push_back(s.name + (s.published() ? "" : s.status == "sampled" ? " [sampled]" : " [tuned]")); }
		for (int c = 0; c < MAX_CHANNELS; c++) {
			int sel = 0;
			for (size_t i = 0; i < ids.size(); i++) if (ids[i] == cfg.channelSpecies[c]) sel = (int)i;
			std::string label = (cfg.embed ? "Slot " : "Channel ") + ofToString(c);
			if (ImGui::BeginCombo(label.c_str(), names[sel].c_str())) {
				for (size_t i = 0; i < ids.size(); i++)
					if (ImGui::Selectable(names[i].c_str(), (int)i == sel)) {
						const std::string old = cfg.channelSpecies[c];
						cfg.channelSpecies[c] = ids[i];
						if (!applyWorldConfig(false)) { cfg.channelSpecies[c] = old; applyWorldConfig(false); }
					}
				ImGui::EndCombo();
			}
			if (const Species * sp = channelSpecies(c)) {
				ImGui::TextWrapped("  %s; R %.0f -> %.0f mm, dt %.3f. %s", statusTag(sp->status), sp->R,
					sp->R * p.rScale * p.cellMm, sp->dt, sp->notes.c_str());
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", sp->source.c_str());
			}
		}
		// worlds from the file
		std::vector<int> wl;
		for (size_t i = 0; i < lib.worlds.size(); i++) if (lib.worlds[i].model == cfg.model) wl.push_back((int)i);
		if (!wl.empty()) {
			if (worldChoice < 0 || worldChoice >= (int)lib.worlds.size() || lib.worlds[worldChoice].model != cfg.model) worldChoice = wl[0];
			if (ImGui::BeginCombo("World", lib.worlds[worldChoice].name.c_str())) {
				for (int i : wl) if (ImGui::Selectable(lib.worlds[i].name.c_str(), i == worldChoice)) worldChoice = i;
				ImGui::EndCombo();
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s", lib.worlds[worldChoice].source.c_str(), lib.worlds[worldChoice].notes.c_str());
			if (ImGui::Button("Load world (and place one organism per channel)")) {
				const WorldDef & d = lib.worlds[worldChoice];
				WorldConfig c;
				c.model = d.model;
				c.embed = d.embed;
				for (size_t i = 0; i < d.channelSpecies.size() && i < MAX_CHANNELS; i++) c.channelSpecies[i] = d.channelSpecies[i];
				c.crossKernels = d.crossKernels;
				const WorldConfig old = cfg;
				cfg = c;
				if (!applyWorldConfig(false)) { cfg = old; applyWorldConfig(false); }
				else if (ensureWorld()) {
					resetWorld();
					int placed = 0;
					for (int ch = 0; ch < MAX_CHANNELS; ch++)
						if (channelSpecies(ch)) {
							stampSpecies(ch, ofVec2f((0.3f + 0.4f * (placed % 2)) * worldWc, (0.45f + 0.1f * (placed / 2)) * worldHc), placed % 2 ? 180.0f : 0.0f);
							placed++;
						}
				}
			}
			ImGui::TextWrapped("%s", statusTag(lib.worlds[worldChoice].status));
		}
	}

	// --- time and space
	if (ImGui::CollapsingHeader("Time and space", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Pause", &p.paused);
		ImGui::SameLine();
		if (ImGui::Button("Step") && ensureWorld()) { ofPushStyle(); ofEnableBlendMode(OF_BLENDMODE_DISABLED); step(); ofPopStyle(); }
		ImGui::SliderFloat("Time scale (time units / s)", &p.timeScale, 0.05f, 10.0f, "%.2f");
		ImGui::SliderFloat("dt (per step)", &p.dt, 0.01f, 0.5f, "%.3f");
		ImGui::Text("Steps per second: %.1f (cap %d per frame)", p.timeScale / std::max(1e-3f, p.dt), p.maxStepsPerFrame);
		ImGui::SliderInt("Max steps per frame", &p.maxStepsPerFrame, 1, 32);
		bool resize = false;
		resize |= ImGui::SliderFloat("Cell size (mm)", &p.cellMm, 0.5f, 6.0f, "%.2f");
		resize |= ImGui::InputFloat("Projector px per mm", &p.pxPerMm, 0.01f, 0.1f, "%.3f");
		p.pxPerMm = ofClamp(p.pxPerMm, 0.2f, 10.0f);
		if (resize) ImGui::Text("(changing the cell size or scale clears the world)");
		if (ImGui::SliderFloat("R scale (kernels and patterns)", &p.rScale, 0.25f, 2.0f, "%.2f")) rebuildKernels();
		ImGui::Checkbox("Wrap edges (torus; off = walls of the box)", &p.wrapWorld);
		ImGui::Checkbox("Show 100 mm scale bar", &p.scaleBar);
		for (int c = 0; c < MAX_CHANNELS; c++)
			if (const Species * sp = channelSpecies(c))
				ImGui::Text("%s %d: kernel radius %.0f mm; organism ~%.0f mm (pattern %.0f cells)", cfg.embed ? "Slot" : "Ch", c,
					sp->R * p.rScale * p.cellMm, (sp->cells.empty() ? sp->noisePatch : std::max(sp->cells[0].w, sp->cells[0].h)) * p.rScale * p.cellMm,
					(sp->cells.empty() ? sp->noisePatch : std::max(sp->cells[0].w, sp->cells[0].h)) * p.rScale);
	}

	// --- kernels
	if (ImGui::CollapsingHeader("Kernels, growth and cross-channel weights")) {
		ImGui::Text("%d kernels, %d taps, max radius %d cells; rings cut: %.3f %% of weight",
			(int)setupW.kernels.size(), setupW.table.count, setupW.table.maxRadius, 100.0 * setupW.maxTruncated);
		drawKernelGui();
		if (ImGui::Button("Reload species values")) applyWorldConfig(false);
	}

	// --- tools
	if (ImGui::CollapsingHeader("Spawn, brush and seeding", ImGuiTreeNodeFlags_DefaultOpen)) {
		static const char * tools[] = { "Spawn preset", "Add mass", "Erase (hand)", "Paint mask (inside)", "Erase mask", "Paint gradient", "Erase gradient" };
		ImGui::Combo("Left mouse", &p.tool, tools, 7);
		ImGui::SliderInt(cfg.embed ? "Spawn slot" : "Spawn / brush channel", &p.spawnChannel, 0, 2);
		ImGui::Checkbox("Random rotation", &p.spawnRandomRot);
		if (!p.spawnRandomRot) ImGui::SliderFloat("Rotation (deg; keys [ ])", &p.spawnDeg, 0.0f, 360.0f, "%.0f");
		ImGui::SliderFloat("Brush radius (mm)", &p.brushMm, 3.0f, 150.0f, "%.0f");
		ImGui::SliderFloat("Brush strength", &p.brushAmount, 0.05f, 1.0f, "%.2f");
		ImGui::SliderInt("Count", &p.scatterCount, 1, 40);
		if (ImGui::Button("Scatter organisms") && ensureWorld()) { ofPushStyle(); ofEnableBlendMode(OF_BLENDMODE_DISABLED); scatter(); ofPopStyle(); }
		ImGui::SameLine();
		if (ImGui::Button("Noise patches") && ensureWorld()) { ofPushStyle(); ofEnableBlendMode(OF_BLENDMODE_DISABLED); noisePatches(); ofPopStyle(); }
		ImGui::SliderFloat("Noise patch (mm)", &p.noiseMm, 10.0f, 200.0f, "%.0f");
		if (ImGui::Button("Reset (clear)")) resetWorld();
	}

	// --- mask
	if (ImGui::CollapsingHeader("Boundary mask (stand-in for the water basin)")) {
		static const char * masks[] = { "None", "Circle", "Painted" };
		ImGui::Combo("Mask", &p.maskMode, masks, 3);
		if (p.maskMode == MASK_CIRCLE) {
			ImGui::SliderFloat("Radius (mm)", &p.maskRadiusMm, 30.0f, 600.0f, "%.0f");
			ImGui::SliderFloat("Centre x", &p.maskCentreX, 0.0f, 1.0f);
			ImGui::SliderFloat("Centre y", &p.maskCentreY, 0.0f, 1.0f);
		}
		if (p.maskMode == MASK_PAINTED) {
			ImGui::Text("Left mouse: Paint mask / Erase mask tools.");
			if (ImGui::Button("Fill (all inside)") && ensureWorld()) { envBuf[envCur].begin(); ofClear(255, 0, 0, 0); envBuf[envCur].end(); }
			ImGui::SameLine();
			if (ImGui::Button("Clear (all outside)") && ensureWorld()) { envBuf[envCur].begin(); ofClear(0, 0, 0, 0); envBuf[envCur].end(); }
		}
		ImGui::SliderFloat("Soft edge (mm)", &p.maskEdgeMm, 2.0f, 60.0f, "%.0f");
		if (cfg.model == MODEL_LENIA)
			ImGui::SliderFloat("Growth outside (0 none, <0 dissolves)", &p.outsideGrowth, -1.0f, 0.0f, "%.2f");
		else
			ImGui::SliderFloat("Edge repulsion (cells / time unit)", &p.edgeRepel, 0.0f, 10.0f, "%.2f");
		ImGui::SliderFloat("Show mask edge", &p.maskShow, 0.0f, 1.0f);
	}

	// --- gradient
	if (ImGui::CollapsingHeader("Environment gradient (food, shade, depth later)")) {
		static const char * grads[] = { "Off", "Linear", "Painted" };
		ImGui::Combo("Gradient", &p.gradMode, grads, 3);
		if (p.gradMode == GRAD_LINEAR) ImGui::SliderFloat("Direction (deg, towards higher)", &p.gradDeg, 0.0f, 360.0f, "%.0f");
		if (p.gradMode == GRAD_PAINTED) {
			ImGui::Text("Left mouse: Paint gradient / Erase gradient tools.");
			if (ImGui::Button("Clear gradient") && ensureWorld()) {
				ofFloatPixels px;
				envBuf[envCur].readToPixels(px);
				// keep the mask (r), zero the gradient (g)
				for (size_t i = 0; i + 3 < px.size(); i += px.getNumChannels()) px[i + 1] = 0.0f;
				envBuf[envCur].getTexture().loadData(px);
			}
		}
		static const char * targets[] = { "Growth / affinity bias", "Drift (advection / flow)" };
		ImGui::Combo("Acts on", &p.gradTarget, targets, 2);
		ImGui::SliderFloat("Strength", &p.gradStrength, 0.0f, 2.0f, "%.3f");
		ImGui::TextWrapped(p.gradTarget == 0
			? "Lenia: growth + strength (E - 0.5) on occupied cells. Flow-Lenia: affinity + strength (E - 0.5), so matter flows up E."
			: "Drift: matter is carried up the gradient at 'strength' cells per time unit (imposed, not emergent).");
		ImGui::SliderFloat("Show gradient", &p.gradShow, 0.0f, 1.0f);
	}

	// --- drawing
	if (ImGui::CollapsingHeader("Drawing")) {
		for (int c = 0; c < 3; c++)
			ImGui::ColorEdit3((std::string(cfg.embed ? "Slot " : "Channel ") + ofToString(c) + " colour").c_str(), &p.colour[c].r);
		ImGui::Text("(no saturated red beside saturated green)");
		ImGui::SliderFloat("Gain", &p.gain, 0.2f, 6.0f);
		ImGui::SliderFloat("Gamma", &p.gammaExp, 0.3f, 2.0f);
		ImGui::SliderFloat("Glow", &p.glow, 0.0f, 1.5f);
		ImGui::SliderFloat("Glow radius (mm)", &p.glowMm, 1.0f, 40.0f, "%.0f");
		ImGui::Checkbox("Trail", &p.trailOn);
		if (p.trailOn) ImGui::SliderFloat("Trail time (s)", &p.trailSec, 0.05f, 3.0f, "%.2f");
		dither.drawGui();
	}

	// --- readouts
	if (ImGui::CollapsingHeader("Readouts", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text("Creatures (blobs): %d", creatures);
		for (int c = 0; c < 3; c++) {
			if (setupW.channelOn[c] == 0.0f && !(setupW.embed && c == 0)) continue;
			const float d = massAtReset[c] > 0 ? 100.0f * (mass[c] - massAtReset[c]) / massAtReset[c] : 0.0f;
			ImGui::Text("Mass ch %d: %.2f (%+.4f %% since the last edit)", c, mass[c], d);
			std::vector<float> h(massHistory[c].begin(), massHistory[c].end());
			if (!h.empty()) {
				float lo = *std::min_element(h.begin(), h.end()), hi = *std::max_element(h.begin(), h.end());
				if (hi - lo < 1e-3f * std::max(1.0f, hi)) { lo -= 1; hi += 1; }
				ImGui::PlotLines(("##mass" + ofToString(c)).c_str(), h.data(), (int)h.size(), 0, nullptr, lo, hi, ImVec2(0, 50));
			}
		}
		if (ImGui::Button("Mark mass")) massResetPending = true;
	}
	ImGui::PopID();
	ImGui::End();
}
