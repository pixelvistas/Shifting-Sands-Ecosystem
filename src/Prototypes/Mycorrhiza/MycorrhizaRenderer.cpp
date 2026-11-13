#include "MycorrhizaRenderer.h"
#include "../../KinectProjector/KinectProjector.h"
#include "../../Games/VegetationField.h"
#include "ofxImGui.h"

// Display defaults (DEVLOG; chosen on tools/mycorrhiza_harness, not yet on the sandbox).
bool MycorrhizaRenderer::OVERLAY_ON_SANDBOX = false;

// "Patches and hyphae" (2026-11-13). Colours from the brief; no saturated
// green anywhere, so no red-green adjacency.
float MycorrhizaRenderer::PATCH_SCALE_MM = 40.0f;
float MycorrhizaRenderer::PATCH_FLOW = 0.15f;
float MycorrhizaRenderer::PATCH_CONTRAST = 1.5f;
float MycorrhizaRenderer::PATCH_BRIGHTNESS = 1.1f;
float MycorrhizaRenderer::EDGE_COOL = 0.7f;
float MycorrhizaRenderer::RIM_GAIN = 0.35f;
float MycorrhizaRenderer::STOP_POS[4] = { 0.0f, 0.33f, 0.66f, 1.0f };
ofFloatColor MycorrhizaRenderer::STOP_COLOR[4] = {
	ofFloatColor(0x3F / 255.0f, 0xD8 / 255.0f, 0xFF / 255.0f),  // cyan
	ofFloatColor(0xCF / 255.0f, 0xEF / 255.0f, 0xFF / 255.0f),  // blue-white
	ofFloatColor(0xFF / 255.0f, 0xB3 / 255.0f, 0x47 / 255.0f),  // amber
	ofFloatColor(0xF0 / 255.0f, 0x7A / 255.0f, 0x2A / 255.0f) };// orange
ofFloatColor MycorrhizaRenderer::HYPHA_PALE = ofFloatColor(0xE6 / 255.0f, 0xF7 / 255.0f, 0xFF / 255.0f);
ofFloatColor MycorrhizaRenderer::HYPHA_WARM = ofFloatColor(0xFF / 255.0f, 0xE2 / 255.0f, 0xB8 / 255.0f);
float MycorrhizaRenderer::WARM_GAIN = 4.0f;
float MycorrhizaRenderer::HYPHA_BRIGHTNESS = 0.85f;
float MycorrhizaRenderer::GLOW_RADIUS_PX = 2.0f;
float MycorrhizaRenderer::GLOW_STRENGTH = 0.7f;
// Liquid look (2026-11-14): marbling, flow map, gloss.
float MycorrhizaRenderer::WARP = 2.5f;
float MycorrhizaRenderer::STREAK = 0.45f;
float MycorrhizaRenderer::BANDS = 6.0f;
float MycorrhizaRenderer::RIDGE_WIDTH = 0.35f;
float MycorrhizaRenderer::FLOW_CYCLE = 2.0f;
float MycorrhizaRenderer::V_MAX = 30.0f;
float MycorrhizaRenderer::H_MAX = 12.0f;
float MycorrhizaRenderer::BUMP = 3.0f;
float MycorrhizaRenderer::SPEC_GAIN = 0.55f;
float MycorrhizaRenderer::SHININESS = 28.0f;
ofFloatColor MycorrhizaRenderer::TROUGH_COLOR = ofFloatColor(0x0A / 255.0f, 0x2A / 255.0f, 0x6E / 255.0f);

// "Physarum (previous)" (2026-11-13 intermediate pass, unchanged).
float MycorrhizaRenderer::GAMMA = 1.1f;
float MycorrhizaRenderer::CORE_GAIN = 2.0f;
float MycorrhizaRenderer::DETAIL = 0.7f;
float MycorrhizaRenderer::GLOW_RADIUS = 1.5f;
float MycorrhizaRenderer::PHYS_GLOW_STRENGTH = 0.15f;
float MycorrhizaRenderer::BRIGHTNESS = 1.0f;
float MycorrhizaRenderer::BASE_WEIGHT = 0.35f;
float MycorrhizaRenderer::WET_LO = 0.1f;
float MycorrhizaRenderer::WET_HI = 0.4f;
float MycorrhizaRenderer::CORE_MIX = 0.5f;
ofFloatColor MycorrhizaRenderer::COLOR[5] = {
	ofFloatColor(0.38f, 0.16f, 0.80f),
	ofFloatColor(0.20f, 0.85f, 1.00f),
	ofFloatColor(0.95f, 0.25f, 0.80f),
	ofFloatColor(1.00f, 0.55f, 0.15f),
	ofFloatColor(1.00f, 0.95f, 0.90f) };

static const int LUT_STEP = 4;               // grid cells between mesh vertices
static const int VERTS_PER_SEGMENT = 12;     // two quads (either side of the line), two triangles each
static const char * PRESET_FILE = "settings/mycorrhizaPreset.xml";
static const char * PRESET_NAME = "Prototype 2: Mycorrhiza";
typedef MycorrhizaField MF;

static bool loadPair(ofShader & s, const std::string & dir, const std::string & name)
{
	return s.load(dir + name + ".vert", dir + name + ".frag");
}

void MycorrhizaRenderer::loadShaders()
{
#ifdef TARGET_OPENGLES
	shaderLoaded = layerShadersLoaded = false;
#else
	const std::string dir = ofIsGLProgrammableRenderer() ? "shaders/mycorrhiza/GL3/" : "shaders/mycorrhiza/GL2/";
	shaderLoaded = loadPair(shader, dir, "mycorrhiza");
	layerShadersLoaded = loadPair(patchShader, dir, "patches") && loadPair(hyphaShader, dir, "hypha") && loadPair(glowShader, dir, "glow");
#endif
	if (!shaderLoaded || !layerShadersLoaded)
		ofLogError("MycorrhizaRenderer") << "setup(): could not load shaders/mycorrhiza (physarum " << shaderLoaded
			<< ", patches/hyphae/glow " << layerShadersLoaded << ")";
}

void MycorrhizaRenderer::setup(std::shared_ptr<KinectProjector> const & kp, const VegetationField * vf)
{
	kinectProjector = kp;
	vegetation = vf;
	loadShaders();
	if (ofFile::doesFileExist(PRESET_FILE))
		loadPreset();
	clear();
}

void MycorrhizaRenderer::setProjectorRes(const ofVec2f & res)
{
	projRes = res;
	const int w = std::max(1, (int)projRes.x), h = std::max(1, (int)projRes.y);
	fbo.allocate(w, h, GL_RGBA);
	hyphaFbo.allocate(w, h, GL_RGBA);
	for (ofFbo * f : { &fbo, &hyphaFbo }) {
		f->begin();
		ofClear(0, 0, 0, 255);
		f->end();
	}
}

void MycorrhizaRenderer::clear()
{
	field.clear();
	hasFrame = false;
	ringCap = 0;       // the ring is reallocated (empty) on the next update
	ringNextId = 0;
	tipMesh.clear();
	for (ofFbo * f : { &fbo, &hyphaFbo })
		if (f->isAllocated()) {
			f->begin();
			ofClear(0, 0, 0, 255);
			f->end();
		}
}

void MycorrhizaRenderer::gatherInputs(MF::Inputs & in)
{
	const int cols = vegetation->getCols(), rows = vegetation->getRows();
	const cv::Mat & elev = vegetation->getElevationGrid();
	if (cols <= 0 || rows <= 0 || elev.rows != rows || elev.cols != cols || elev.type() != CV_32F || !elev.isContinuous())
		return;
	waterMask.resize((size_t)cols * rows);
	for (int gy = 0; gy < rows; gy++)
		for (int gx = 0; gx < cols; gx++)
			waterMask[(size_t)gy * cols + gx] = vegetation->isWaterCell(gx, gy) ? 1 : 0;
	in.elevationMM = elev.ptr<float>(0);
	in.water = waterMask.data();
	in.cols = cols;
	in.rows = rows;
	in.mmPerCell = vegetation->getMmPerCell();
	float dt = ofGetLastFrameTime();
	in.dt = (dt > 0.0f && dt <= 0.1f) ? dt : 1.0f / 60.0f;
	// Succession fields only when that model is running and sized to this grid.
	const SuccessionModel & sm = vegetation->getSuccession();
	if (VegetationField::VEGETATION_MODEL == VegetationField::MODEL_SITE_FEEDBACK && sm.getCols() == cols && sm.getRows() == rows) {
		for (int r = 0; r < 3; r++)
			in.cover[r] = sm.cover(r);
		in.soil = sm.soil();
		in.moisture = sm.moisture();
	}
}

void MycorrhizaRenderer::update()
{
	if (!kinectProjector || !vegetation)
		return;
	if (!kinectProjector->isImageStabilized()) {
		// No fresh depth: keep the network, re-adopt the surface when it returns.
		field.restartTouch();
		return;
	}
	MF::Inputs in;
	gatherInputs(in);
	if (!in.elevationMM)
		return;
	field.step(in);
	rebuildMesh();
	if (MF::STYLE == MF::STYLE_PHYSARUM) {
		uploadTrace();
		renderPhysarum();
	} else {
		uploadPatches();
		syncHyphaRing();
		buildTipMesh();
		renderLayers();
	}
	hasFrame = true;
}

// Grid vertices every LUT_STEP cells at their projector positions
// (kinectCoordToProjCoord depends on the current depth, so rebuilt each
// frame as SeedLayer/WaterFlowField do). Texcoord = grid cell coordinate of
// the cell centre. Also the lookup for toProj().
void MycorrhizaRenderer::rebuildMesh()
{
	const uint64_t t0 = ofGetElapsedTimeMicros();
	const int cols = field.getCols(), rows = field.getRows();
	const int mw = (cols - 1 + LUT_STEP - 1) / LUT_STEP + 1, mh = (rows - 1 + LUT_STEP - 1) / LUT_STEP + 1;
	const ofVec2f origin = vegetation->getGridOrigin();
	const float step = vegetation->getGridStep();
	if (mw != meshCols || mh != meshRows || mesh.getNumVertices() != (size_t)mw * mh) {
		mesh.clear();
		mesh.setMode(OF_PRIMITIVE_TRIANGLES);
		for (int j = 0; j < mh; j++)
			for (int i = 0; i < mw; i++) {
				mesh.addVertex(glm::vec3(0, 0, 0));
				mesh.addTexCoord(glm::vec2(0, 0));
			}
		for (int j = 0; j + 1 < mh; j++)
			for (int i = 0; i + 1 < mw; i++) {
				const ofIndexType a = j * mw + i, b = a + 1, c = a + mw, d = c + 1;
				mesh.addIndex(a); mesh.addIndex(b); mesh.addIndex(c);
				mesh.addIndex(b); mesh.addIndex(d); mesh.addIndex(c);
			}
		meshCols = mw;
		meshRows = mh;
	}
	for (int j = 0; j < mh; j++)
		for (int i = 0; i < mw; i++) {
			const float gx = (float)std::min(i * LUT_STEP, cols - 1), gy = (float)std::min(j * LUT_STEP, rows - 1);
			const ofVec2f p = kinectProjector->kinectCoordToProjCoord(origin.x + gx * step, origin.y + gy * step);
			const size_t v = (size_t)j * mw + i;
			mesh.setVertex(v, glm::vec3(p.x, p.y, 0));
			mesh.setTexCoord(v, glm::vec2(gx + 0.5f, gy + 0.5f));
		}
	lastMeshMs = (ofGetElapsedTimeMicros() - t0) / 1000.0f;
}

// Projector position of grid coordinate (gx, gy), bilinear in the mesh.
ofVec2f MycorrhizaRenderer::toProj(float gx, float gy) const
{
	if (meshCols < 2 || meshRows < 2)
		return ofVec2f(0, 0);
	const float fx = (gx - 0.5f) / LUT_STEP, fy = (gy - 0.5f) / LUT_STEP;
	const int i0 = std::max(0, std::min((int)std::floor(fx), meshCols - 2)), j0 = std::max(0, std::min((int)std::floor(fy), meshRows - 2));
	const float tx = std::min(1.0f, std::max(0.0f, fx - i0)), ty = std::min(1.0f, std::max(0.0f, fy - j0));
	const glm::vec3 & a = mesh.getVertices()[(size_t)j0 * meshCols + i0];
	const glm::vec3 & b = mesh.getVertices()[(size_t)j0 * meshCols + i0 + 1];
	const glm::vec3 & c = mesh.getVertices()[(size_t)(j0 + 1) * meshCols + i0];
	const glm::vec3 & d = mesh.getVertices()[(size_t)(j0 + 1) * meshCols + i0 + 1];
	const glm::vec3 p = (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty;
	return ofVec2f(p.x, p.y);
}

// ------------------------------------------------------------ patches and hyphae
// RGBA8: R presence, G maturity (per cell); warmth per touch block.
void MycorrhizaRenderer::uploadPatches()
{
	const uint64_t t0 = ofGetElapsedTimeMicros();
	const int cols = field.getCols(), rows = field.getRows();
	auto prep = [](ofTexture & tex, ofPixels & pix, int w, int h) {
		if (!pix.isAllocated() || (int)pix.getWidth() != w || (int)pix.getHeight() != h)
			pix.allocate(w, h, OF_PIXELS_RGBA);
		if (!tex.isAllocated() || (int)tex.getWidth() != w || (int)tex.getHeight() != h) {
			tex.allocate(w, h, GL_RGBA8, true); // rectangle texture: texel coordinates
			tex.setTextureMinMagFilter(GL_LINEAR, GL_LINEAR);
			tex.setTextureWrap(GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
		}
	};
	auto q = [](float v) { return (unsigned char)std::lround(std::min(1.0f, std::max(0.0f, v)) * 255.0f); };
	prep(patchTex, patchPix, cols, rows);
	const float * P = field.presence();
	const float * M = field.maturity();
	unsigned char * p = patchPix.getData();
	const size_t n = (size_t)cols * rows;
	for (size_t i = 0; i < n; i++) {
		p[4 * i + 0] = q(P[i]);
		p[4 * i + 1] = q(M[i]);
		p[4 * i + 2] = 0;
		p[4 * i + 3] = 255;
	}
	patchTex.loadData(patchPix);
	const int bwN = field.getBlockCols(), bhN = field.getBlockRows();
	prep(warmTex, warmPix, bwN, bhN);
	const float * W = field.warmthBlocks();
	unsigned char * w = warmPix.getData();
	for (size_t i = 0; i < (size_t)bwN * bhN; i++) {
		w[4 * i + 0] = w[4 * i + 1] = w[4 * i + 2] = q(WARM_GAIN * W[i]);
		w[4 * i + 3] = 255;
	}
	warmTex.loadData(warmPix);
	// Light-liquid, half resolution: R, G velocity (0.5 + v / (2 V_MAX)), B depth / H_MAX.
	const int lwN = field.getLiquidCols(), lhN = field.getLiquidRows();
	prep(flowTex, flowPix, lwN, lhN);
	const float vInv = 1.0f / (2.0f * std::max(1.0f, V_MAX)), hInv = 1.0f / std::max(0.1f, H_MAX);
	unsigned char * fl = flowPix.getData();
	for (size_t i = 0; i < (size_t)lwN * lhN; i++) {
		fl[4 * i + 0] = q(0.5f + field.liquidVelX()[i] * vInv);
		fl[4 * i + 1] = q(0.5f + field.liquidVelY()[i] * vInv);
		fl[4 * i + 2] = q(field.liquidDepth()[i] * hInv);
		fl[4 * i + 3] = 255;
	}
	flowTex.loadData(flowPix);
	lastUploadMs = (ofGetElapsedTimeMicros() - t0) / 1000.0f;
}

// Twelve vertices per segment: centre line (across 1) and an outer edge
// (across 0) on each side, offset by the segment width in projector px.
// Positions are mapped when the segment is written (the sand under a
// thread rarely moves after it has grown; a cleared network rebuilds).
void MycorrhizaRenderer::writeSegment(size_t slot, const MF::Segment & s)
{
	glm::vec3 * P = &ringPos[slot * VERTS_PER_SEGMENT];
	glm::vec2 * T = &ringTex[slot * VERTS_PER_SEGMENT];
	glm::vec3 * N = &ringNorm[slot * VERTS_PER_SEGMENT];
	if (s.width <= 0.0f) {
		for (int k = 0; k < VERTS_PER_SEGMENT; k++) { P[k] = glm::vec3(0); T[k] = glm::vec2(0); N[k] = glm::vec3(0); }
		return;
	}
	const ofVec2f a = toProj(s.x0, s.y0), b = toProj(s.x1, s.y1);
	glm::vec2 d(b.x - a.x, b.y - a.y);
	const float len = glm::length(d);
	d = len > 1e-4f ? d / len : glm::vec2(1, 0);
	const glm::vec2 nrm = glm::vec2(-d.y, d.x) * s.width;
	const glm::vec3 A(a.x, a.y, 0), B(b.x, b.y, 0);
	const glm::vec3 g0(s.x0, s.y0, 0), g1(s.x1, s.y1, 0);
	int k = 0;
	for (float side : { 1.0f, -1.0f }) {
		const glm::vec3 o(nrm.x * side, nrm.y * side, 0);
		const glm::vec3 pos[6] = { A, B, A + o, B, B + o, A + o };
		const float across[6] = { 1, 1, 0, 1, 0, 0 };
		const glm::vec3 grid[6] = { g0, g1, g0, g1, g1, g0 };
		for (int v = 0; v < 6; v++, k++) {
			P[k] = pos[v];
			T[k] = glm::vec2(s.birth, across[v]);
			N[k] = grid[v];
		}
	}
}

void MycorrhizaRenderer::zeroSlot(size_t slot)
{
	MF::Segment z{ 0, 0, 0, 0, 0.0f, 0, 0 };
	writeSegment(slot, z);
}

void MycorrhizaRenderer::uploadRing(size_t slot0, size_t count)
{
	if (count == 0) return;
	const size_t v0 = slot0 * VERTS_PER_SEGMENT, nv = count * VERTS_PER_SEGMENT;
	ringVbo.getVertexBuffer().updateData(v0 * sizeof(glm::vec3), nv * sizeof(glm::vec3), &ringPos[v0]);
	ringVbo.getTexCoordBuffer().updateData(v0 * sizeof(glm::vec2), nv * sizeof(glm::vec2), &ringTex[v0]);
	ringVbo.getNormalBuffer().updateData(v0 * sizeof(glm::vec3), nv * sizeof(glm::vec3), &ringNorm[v0]);
}

// Segment id i lives in slot i % ringCap. Only segments grown since the
// last frame are written; segments removed for water are zeroed. Segments
// that fade out simply stay dim until their slot is reused.
void MycorrhizaRenderer::syncHyphaRing()
{
	const uint64_t t0 = ofGetElapsedTimeMicros();
	const std::deque<MF::Segment> & segs = field.segments();
	const size_t cap = (size_t)std::max(1, MF::MAX_SEGMENTS);
	if (cap != ringCap) { // (re)allocate empty, then write every live segment
		ringCap = cap;
		ringPos.assign(cap * VERTS_PER_SEGMENT, glm::vec3(0));
		ringTex.assign(cap * VERTS_PER_SEGMENT, glm::vec2(0));
		ringNorm.assign(cap * VERTS_PER_SEGMENT, glm::vec3(0));
		for (const MF::Segment & s : segs)
			writeSegment((size_t)(s.id % cap), s);
		ringVbo.clear();
		ringVbo.setVertexData(ringPos.data(), (int)ringPos.size(), GL_DYNAMIC_DRAW);
		ringVbo.setTexCoordData(ringTex.data(), (int)ringTex.size(), GL_DYNAMIC_DRAW);
		ringVbo.setNormalData(ringNorm.data(), (int)ringNorm.size(), GL_DYNAMIC_DRAW);
		ringNextId = segs.empty() ? 0 : segs.back().id + 1;
		lastRingMs = (ofGetElapsedTimeMicros() - t0) / 1000.0f;
		return;
	}
	// New segments: at the back of the deque, ids >= ringNextId.
	if (!segs.empty() && segs.back().id + 1 > ringNextId) {
		size_t first = segs.size();
		while (first > 0 && segs[first - 1].id >= ringNextId) first--;
		const uint64_t id0 = segs[first].id, id1 = segs.back().id + 1;
		for (size_t k = first; k < segs.size(); k++)
			writeSegment((size_t)(segs[k].id % cap), segs[k]);
		// Upload as one or two contiguous slot ranges.
		const size_t n = (size_t)std::min<uint64_t>(id1 - id0, cap);
		const size_t s0 = (size_t)(id0 % cap);
		const size_t part = std::min(n, cap - s0);
		uploadRing(s0, part);
		uploadRing(0, n - part);
		ringNextId = id1;
	}
	for (uint64_t id : field.segmentsRemovedThisStep()) {
		const size_t slot = (size_t)(id % cap);
		zeroSlot(slot);
		uploadRing(slot, 1);
	}
	lastRingMs = (ofGetElapsedTimeMicros() - t0) / 1000.0f;
}

// The part of each growing tip not yet stored as a segment (born "now").
void MycorrhizaRenderer::buildTipMesh()
{
	tipMesh.clear();
	tipMesh.setMode(OF_PRIMITIVE_TRIANGLES);
	const float now = (float)field.getTime();
	for (const MF::Tip & t : field.tips()) {
		const ofVec2f a = toProj(t.sx, t.sy), b = toProj(t.x, t.y);
		glm::vec2 d(b.x - a.x, b.y - a.y);
		const float len = glm::length(d);
		if (len < 0.05f) continue;
		d /= len;
		const glm::vec2 nrm = glm::vec2(-d.y, d.x) * t.width;
		const glm::vec3 A(a.x, a.y, 0), B(b.x, b.y, 0);
		for (float side : { 1.0f, -1.0f }) {
			const glm::vec3 o(nrm.x * side, nrm.y * side, 0);
			const glm::vec3 pos[6] = { A, B, A + o, B, B + o, A + o };
			const float across[6] = { 1, 1, 0, 1, 0, 0 };
			const glm::vec3 grid[6] = { glm::vec3(t.sx, t.sy, 0), glm::vec3(t.x, t.y, 0), glm::vec3(t.sx, t.sy, 0),
				glm::vec3(t.x, t.y, 0), glm::vec3(t.x, t.y, 0), glm::vec3(t.sx, t.sy, 0) };
			for (int v = 0; v < 6; v++) {
				tipMesh.addVertex(pos[v]);
				tipMesh.addTexCoord(glm::vec2(now, across[v]));
				tipMesh.addNormal(grid[v]);
			}
		}
	}
}

void MycorrhizaRenderer::renderLayers()
{
	if (!fbo.isAllocated() || !hyphaFbo.isAllocated() || !layerShadersLoaded)
		return;
	const float now = (float)field.getTime();
	// 1. Hyphae into their own FBO, GL_MAX so the joints of a thread do not double up.
	hyphaFbo.begin();
	ofClear(0, 0, 0, 255);
	ofPushStyle();
	ofEnableBlendMode(OF_BLENDMODE_DISABLED);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE);
	glBlendEquation(GL_MAX);
	ofSetColor(255);
	hyphaShader.begin();
	hyphaShader.setUniformTexture("warmTex", warmTex, 1);
	hyphaShader.setUniform1f("time", now);
	hyphaShader.setUniform1f("memSec", std::max(0.05f, MF::TRACE_MEMORY_MIN) * 60.0f);
	hyphaShader.setUniform1f("blockSize", (float)std::max(1, field.getBlockSize()));
	hyphaShader.setUniform1f("brightness", HYPHA_BRIGHTNESS);
	hyphaShader.setUniform3f("pale", HYPHA_PALE.r, HYPHA_PALE.g, HYPHA_PALE.b);
	hyphaShader.setUniform3f("warm", HYPHA_WARM.r, HYPHA_WARM.g, HYPHA_WARM.b);
	const uint64_t written = std::min<uint64_t>(ringNextId, ringCap);
	if (written > 0)
		ringVbo.draw(GL_TRIANGLES, 0, (int)(written * VERTS_PER_SEGMENT));
	if (tipMesh.getNumVertices() > 0)
		tipMesh.draw();
	hyphaShader.end();
	glBlendEquation(GL_FUNC_ADD);
	ofPopStyle();
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
	hyphaFbo.end();

	// 2. Patches over black, then the hyphae and their glow added on top.
	fbo.begin();
	ofClear(0, 0, 0, 255);
	ofPushStyle();
	ofEnableBlendMode(OF_BLENDMODE_DISABLED);
	ofSetColor(255);
	if (patchTex.isAllocated()) {
		patchShader.begin();
		patchShader.setUniformTexture("patchTex", patchTex, 1);
		patchShader.setUniformTexture("flowTex", flowTex, 2);
		patchShader.setUniform1f("warp", WARP);
		patchShader.setUniform1f("streak", STREAK);
		patchShader.setUniform1f("bands", std::max(0.5f, BANDS));
		patchShader.setUniform1f("ridgeWidth", ofClamp(RIDGE_WIDTH, 0.01f, 1.0f));
		patchShader.setUniform1f("flowCycle", std::max(0.1f, FLOW_CYCLE));
		patchShader.setUniform1f("vMax", std::max(1.0f, V_MAX));
		patchShader.setUniform1f("hMax", std::max(0.1f, H_MAX));
		patchShader.setUniform1f("bump", BUMP);
		patchShader.setUniform1f("specGain", SPEC_GAIN);
		patchShader.setUniform1f("shininess", std::max(1.0f, SHININESS));
		patchShader.setUniform3f("trough", TROUGH_COLOR.r, TROUGH_COLOR.g, TROUGH_COLOR.b);
		patchShader.setUniform1f("time", now);
		patchShader.setUniform1f("cellMM", std::max(1e-3f, vegetation->getMmPerCell()));
		patchShader.setUniform1f("scaleMM", std::max(1.0f, PATCH_SCALE_MM));
		patchShader.setUniform1f("flowSpeed", PATCH_FLOW);
		patchShader.setUniform1f("contrast", PATCH_CONTRAST);
		patchShader.setUniform1f("brightness", PATCH_BRIGHTNESS);
		patchShader.setUniform1f("edgeCool", EDGE_COOL);
		patchShader.setUniform1f("rimGain", RIM_GAIN);
		float pos[4];
		for (int i = 0; i < 4; i++) pos[i] = i == 0 ? STOP_POS[0] : std::max(pos[i - 1], STOP_POS[i]);
		patchShader.setUniform4f("stopPos", pos[0], pos[1], pos[2], pos[3]);
		static const char * stops[4] = { "stop0", "stop1", "stop2", "stop3" };
		for (int i = 0; i < 4; i++)
			patchShader.setUniform3f(stops[i], STOP_COLOR[i].r, STOP_COLOR[i].g, STOP_COLOR[i].b);
		mesh.draw();
		patchShader.end();
	}
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE);
	glowShader.begin();
	glowShader.setUniformTexture("src", hyphaFbo.getTexture(), 1);
	glowShader.setUniform1f("radius", std::max(0.0f, GLOW_RADIUS_PX));
	glowShader.setUniform1f("strength", GLOW_STRENGTH);
	ofDrawRectangle(0, 0, fbo.getWidth(), fbo.getHeight());
	glowShader.end();
	ofPopStyle();
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
	fbo.end();
}

// ------------------------------------------------------------ Physarum (previous)
// 8-bit: round(sqrt(clamp(trace / level)) x 255) in R, G, B (the shader reads R); site map as is.
void MycorrhizaRenderer::uploadTrace()
{
	const uint64_t t0 = ofGetElapsedTimeMicros();
	const int cols = field.getCols(), rows = field.getRows();
	if (!tracePix.isAllocated() || (int)tracePix.getWidth() != cols || (int)tracePix.getHeight() != rows)
		tracePix.allocate(cols, rows, OF_PIXELS_RGBA);
	for (ofTexture * t : { &traceTex, &siteTex })
		if (!t->isAllocated() || (int)t->getWidth() != cols || (int)t->getHeight() != rows) {
			t->allocate(cols, rows, GL_RGBA8, true);
			t->setTextureMinMagFilter(GL_LINEAR, GL_LINEAR);
			t->setTextureWrap(GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
		}
	const float * t = field.trace();
	const float inv = 1.0f / std::max(1e-6f, field.getDisplayLevel());
	unsigned char * p = tracePix.getData();
	const size_t n = (size_t)cols * rows;
	for (size_t i = 0; i < n; i++) {
		const float v = std::min(1.0f, std::max(0.0f, t[i] * inv));
		const unsigned char e = (unsigned char)std::lround(std::sqrt(v) * 255.0f);
		p[4 * i + 0] = p[4 * i + 1] = p[4 * i + 2] = e;
		p[4 * i + 3] = 255;
	}
	traceTex.loadData(tracePix);
	siteTex.loadData(field.site(), cols, rows, GL_RGBA);
	lastUploadMs = (ofGetElapsedTimeMicros() - t0) / 1000.0f;
}

void MycorrhizaRenderer::renderPhysarum()
{
	if (!fbo.isAllocated())
		return;
	fbo.begin();
	ofClear(0, 0, 0, 255);
	if (shaderLoaded && traceTex.isAllocated()) {
		ofPushStyle();
		ofEnableBlendMode(OF_BLENDMODE_DISABLED);
		ofSetColor(255);
		shader.begin();
		shader.setUniformTexture("traceTex", traceTex, 0);
		shader.setUniformTexture("siteTex", siteTex, 1);
		shader.setUniform1f("gamma", std::max(0.05f, GAMMA));
		shader.setUniform1f("coreGain", CORE_GAIN);
		shader.setUniform1f("detail", ofClamp(DETAIL, 0.0f, 0.95f));
		shader.setUniform1f("glowRadius", GLOW_RADIUS);
		shader.setUniform1f("glowStrength", PHYS_GLOW_STRENGTH);
		shader.setUniform1f("brightness", BRIGHTNESS);
		shader.setUniform1f("baseWeight", std::max(0.01f, BASE_WEIGHT));
		shader.setUniform1f("wetLo", WET_LO);
		shader.setUniform1f("wetHi", std::max(WET_LO + 0.01f, WET_HI));
		shader.setUniform1f("coreMix", CORE_MIX);
		static const char * names[5] = { "colBase", "colWet", "colLife", "colHigh", "colCore" };
		for (int i = 0; i < 5; i++)
			shader.setUniform3f(names[i], COLOR[i].r, COLOR[i].g, COLOR[i].b);
		mesh.draw();
		shader.end();
		ofPopStyle();
		ofEnableBlendMode(OF_BLENDMODE_ALPHA);
	}
	fbo.end();
}

// ------------------------------------------------------------ composite
void MycorrhizaRenderer::composite(float x, float y, float w, float h, bool additive)
{
	if (!fbo.isAllocated())
		return;
	ofPushStyle();
	ofSetColor(255);
	if (additive) {
		if (!hasFrame) { ofPopStyle(); return; }
		ofEnableBlendMode(OF_BLENDMODE_ADD);
	} else {
		ofEnableBlendMode(OF_BLENDMODE_DISABLED);
	}
	fbo.draw(x, y, w, h);
	ofPopStyle();
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
}

void MycorrhizaRenderer::drawMainWindow(float x, float y, float w, float h) { composite(x, y, w, h, false); }
void MycorrhizaRenderer::drawProjectorWindow() { composite(0, 0, projRes.x, projRes.y, false); }
void MycorrhizaRenderer::drawOverlayMainWindow(float x, float y, float w, float h) { composite(x, y, w, h, true); }
void MycorrhizaRenderer::drawOverlayProjectorWindow() { composite(0, 0, projRes.x, projRes.y, true); }

// ------------------------------------------------------------ preset
#define MYC_FIELD_FLOATS(X) \
	X(TOUCH_MM) X(TOUCH_FAST_SEC) X(TOUCH_SLOW_SEC) X(TOUCH_RISE_PER_SEC) X(TOUCH_MEMORY_SEC) X(OCCLUSION_DROP) \
	X(SETTLE_SEC) X(SPAWN_PER_TOUCH) X(SENSOR_ANGLE_DEG) X(SENSOR_DIST_MM) X(ROTATE_DEG) X(STEP_MM) X(MIN_MOBILITY) \
	X(COMB_WEIGHT) X(COMB_SLOPE_SAT) X(CURL_SHARE) X(CURL_SCALE_MM) X(CURL_PERIOD_SEC) X(AGENT_DEPOSIT) \
	X(TRACE_DEPOSIT) X(TRACE_SENSE) X(TRACE_SATURATION) X(FOOD_DEPOSIT) X(TOUCH_DEPOSIT) X(TRAIL_DECAY) \
	X(TRACE_MEMORY_MIN) X(AGENT_LIFE_SEC) X(LEVEL_PERCENTILE) X(LEVEL_FALL_SEC) X(LEVEL_FLOOR) \
	X(HEIGHT_LOW_MM) X(HEIGHT_HIGH_MM) \
	X(PATCH_CELL_MM) X(MATURITY_EASE_SEC) X(MATURITY_TIME_WEIGHT) X(MATURE_SEC) \
	X(HYPHA_SPAWN_PER_SEC) X(HYPHA_SPEED_MM_S) X(TURN_NOISE_DEG) X(ATTRACT_WEIGHT) X(ATTRACT_PLANT) X(BRANCH_PER_MM) \
	X(HYPHA_WIDTH_PX) X(HYPHA_MIN_WIDTH_PX) X(TIP_LIFE_SEC) X(SEGMENT_MM) X(HOME_MM) \
	X(LIQUID_SOURCE_MM_S) X(LIQUID_VISCOSITY) X(LIQUID_PRESENCE_MM) X(HYPHA_TOUCH_BOOST)
#define MYC_FIELD_INTS(X) X(MAX_AGENTS) X(TOUCH_BLOCK) X(THREADS) X(STYLE) X(MAX_TIPS) X(MAX_SEGMENTS) X(MAX_GENERATION) X(LIQUID_SUBSTEPS)
#define MYC_DISPLAY_FLOATS(X) \
	X(PATCH_SCALE_MM) X(PATCH_FLOW) X(PATCH_CONTRAST) X(PATCH_BRIGHTNESS) X(EDGE_COOL) X(RIM_GAIN) X(WARM_GAIN) \
	X(HYPHA_BRIGHTNESS) X(GLOW_RADIUS_PX) X(GLOW_STRENGTH) \
	X(WARP) X(STREAK) X(BANDS) X(RIDGE_WIDTH) X(FLOW_CYCLE) X(V_MAX) X(H_MAX) X(BUMP) X(SPEC_GAIN) X(SHININESS) \
	X(GAMMA) X(CORE_GAIN) X(DETAIL) X(GLOW_RADIUS) X(PHYS_GLOW_STRENGTH) X(BRIGHTNESS) \
	X(BASE_WEIGHT) X(WET_LO) X(WET_HI) X(CORE_MIX)

static void saveColor(ofXml & parent, const std::string & name, const ofFloatColor & c)
{
	auto r = parent.appendChild(name);
	r.appendChild("r").set(c.r);
	r.appendChild("g").set(c.g);
	r.appendChild("b").set(c.b);
}
static void loadColor(const ofXml & parent, const std::string & name, ofFloatColor & c)
{
	auto r = parent.getChild(name);
	if (!r) return;
	if (r.getChild("r")) c.r = r.getChild("r").getFloatValue();
	if (r.getChild("g")) c.g = r.getChild("g").getFloatValue();
	if (r.getChild("b")) c.b = r.getChild("b").getFloatValue();
}

bool MycorrhizaRenderer::savePreset()
{
	ofXml xml;
	auto s = xml.appendChild("MYCORRHIZAPRESET");
	s.appendChild("name").set(std::string(PRESET_NAME));
#define SAVE_F(n) s.appendChild(#n).set(MF::n);
#define SAVE_D(n) s.appendChild(#n).set(n);
	MYC_FIELD_FLOATS(SAVE_F)
	MYC_FIELD_INTS(SAVE_F)
	MYC_DISPLAY_FLOATS(SAVE_D)
#undef SAVE_F
#undef SAVE_D
	for (int i = 0; i < 4; i++) {
		s.appendChild("STOP_POS" + ofToString(i)).set(STOP_POS[i]);
		saveColor(s, "STOP_COLOR" + ofToString(i), STOP_COLOR[i]);
	}
	saveColor(s, "TROUGH_COLOR", TROUGH_COLOR);
	saveColor(s, "HYPHA_PALE", HYPHA_PALE);
	saveColor(s, "HYPHA_WARM", HYPHA_WARM);
	for (int i = 0; i < 5; i++)
		saveColor(s, "color" + ofToString(i), COLOR[i]);
	bool ok = xml.save(PRESET_FILE);
	presetStatus = ok ? std::string("saved \"") + PRESET_NAME + "\"" : "save failed";
	return ok;
}

// Reads only the entries present, so an older file keeps newer defaults.
bool MycorrhizaRenderer::loadPreset()
{
	ofXml xml;
	if (!xml.load(PRESET_FILE)) { presetStatus = "no preset file"; return false; }
	auto s = xml.getChild("MYCORRHIZAPRESET");
	if (!s) { presetStatus = "preset file not recognised"; return false; }
#define LOAD_F(n) if (s.getChild(#n)) MF::n = s.getChild(#n).getFloatValue();
#define LOAD_I(n) if (s.getChild(#n)) MF::n = s.getChild(#n).getIntValue();
#define LOAD_D(n) if (s.getChild(#n)) n = s.getChild(#n).getFloatValue();
	MYC_FIELD_FLOATS(LOAD_F)
	MYC_FIELD_INTS(LOAD_I)
	MYC_DISPLAY_FLOATS(LOAD_D)
#undef LOAD_F
#undef LOAD_I
#undef LOAD_D
	for (int i = 0; i < 4; i++) {
		if (s.getChild("STOP_POS" + ofToString(i))) STOP_POS[i] = s.getChild("STOP_POS" + ofToString(i)).getFloatValue();
		loadColor(s, "STOP_COLOR" + ofToString(i), STOP_COLOR[i]);
	}
	loadColor(s, "TROUGH_COLOR", TROUGH_COLOR);
	loadColor(s, "HYPHA_PALE", HYPHA_PALE);
	loadColor(s, "HYPHA_WARM", HYPHA_WARM);
	for (int i = 0; i < 5; i++)
		loadColor(s, "color" + ofToString(i), COLOR[i]);
	MF::STYLE = std::min(1, std::max(0, MF::STYLE));
	presetStatus = std::string("loaded \"") + s.getChild("name").getValue() + "\"";
	return true;
}

// ------------------------------------------------------------ GUI
void MycorrhizaRenderer::drawGui()
{
	typedef MycorrhizaField F;
	ImGui::Begin("Mycorrhiza");
	ImGui::Text("Prototype 2: a trace of touch. Display only:");
	ImGui::Text("reads the succession model, never writes it.");
	{
		static const char * styles[] = { "Patches and hyphae", "Physarum (previous)" };
		int style = F::STYLE;
		if (ImGui::Combo("Network style", &style, styles, 2) && style != F::STYLE) {
			F::STYLE = style;
			clear();
		}
	}
	if (ImGui::Button("Clear network"))
		clear();
	ImGui::SameLine();
	if (ImGui::Button("Save preset"))
		savePreset();
	ImGui::SameLine();
	if (ImGui::Button("Load preset"))
		loadPreset();
	if (!presetStatus.empty())
		ImGui::Text("Preset: %s", presetStatus.c_str());
	ImGui::Checkbox("Overlay on sandbox (debug)", &OVERLAY_ON_SANDBOX);

	ImGui::Separator();
	ImGui::Text("fps %.1f (note the fps in each display mode)", ofGetFrameRate());
	ImGui::Text("Touched blocks %d; largest block change %.2f mm", field.touchedBlocks(), field.maxTouchChangeMM());
	ImGui::Text("(on untouched sand this must stay below the touch threshold)");
	if (F::STYLE == F::STYLE_PATCHES_HYPHAE) {
		ImGui::Text("Step %.2f ms: touch %.2f, patches %.2f, attractant %.2f, hyphae %.2f", field.getLastStepMs(),
			field.getPhaseMs(0), field.getPhaseMs(1), field.getPhaseMs(2), field.getPhaseMs(3));
		ImGui::Text("Upload %.2f ms, mesh %.2f ms, threads %.2f ms", lastUploadMs, lastMeshMs, lastRingMs);
		ImGui::Text("Tips %d / %d, segments %d / %d, boundary cells %d", (int)field.tips().size(), F::MAX_TIPS,
			(int)field.segments().size(), F::MAX_SEGMENTS, field.boundaryCellCount());
		ImGui::Text("Tips spawned %lld, fusions %lld, patch connections %lld", field.hyphaTipsSpawned(), field.hyphaFusions(),
			field.hyphaPatchConnections());
	} else {
		ImGui::Text("Agents %d / %d, spawned %lld", field.agentCount(), F::MAX_AGENTS, field.spawnedTotal());
		ImGui::Text("Step %.2f ms; upload %.2f, mesh %.2f ms; display level %.1f", field.getLastStepMs(), lastUploadMs, lastMeshMs,
			field.getDisplayLevel());
	}

	if (ImGui::CollapsingHeader("Touch", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderFloat("Touch threshold (mm)", &F::TOUCH_MM, 1.0f, 15.0f);
		ImGui::SliderFloat("Touch memory (s)", &F::TOUCH_MEMORY_SEC, 1.0f, 60.0f);
		ImGui::SliderFloat("Touch fast mean (s)", &F::TOUCH_FAST_SEC, 0.02f, 1.0f);
		ImGui::SliderFloat("Touch slow mean (s)", &F::TOUCH_SLOW_SEC, 0.2f, 10.0f);
		ImGui::SliderFloat("Touch rise (/s)", &F::TOUCH_RISE_PER_SEC, 0.5f, 20.0f);
		ImGui::SliderFloat("Hand occlusion drop", &F::OCCLUSION_DROP, 0.05f, 1.0f);
		ImGui::SliderInt("Touch block (cells)", &F::TOUCH_BLOCK, 2, 8);
		ImGui::SliderFloat("Trace memory (min)", &F::TRACE_MEMORY_MIN, 0.5f, 30.0f);
		ImGui::Text("(fades to about 10 %% in this many minutes)");
		ImGui::SliderFloat("Min mobility (dry, bare)", &F::MIN_MOBILITY, 0.0f, 1.0f);
		ImGui::SliderInt("Threads", &F::THREADS, 1, 8);
	}
	if (F::STYLE == F::STYLE_PATCHES_HYPHAE) {
		if (ImGui::CollapsingHeader("Patches", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::SliderFloat("Patch mark (mm)", &F::PATCH_CELL_MM, 1.0f, 10.0f);
			ImGui::Text("Light-liquid: reshaped sand pours it in; it flows downhill and fills basins.");
			ImGui::SliderFloat("Pour rate (mm/s)", &F::LIQUID_SOURCE_MM_S, 0.0f, 30.0f);
			ImGui::SliderFloat("Viscosity factor", &F::LIQUID_VISCOSITY, 0.0f, 10.0f);
			ImGui::Text("(flux ~ factor x depth^3 x slope: higher flows faster)");
			ImGui::SliderFloat("Fully lit at depth (mm)", &F::LIQUID_PRESENCE_MM, 0.1f, 10.0f);
			ImGui::SliderInt("Liquid substeps", &F::LIQUID_SUBSTEPS, 1, 8);
			ImGui::SliderFloat("Maturity ease (s)", &F::MATURITY_EASE_SEC, 0.2f, 20.0f);
			ImGui::SliderFloat("Maturity time weight", &F::MATURITY_TIME_WEIGHT, 0.0f, 1.0f);
			ImGui::Text("(0: colour is purely the model's recovery of the ground)");
			ImGui::SliderFloat("Mature after (s)", &F::MATURE_SEC, 2.0f, 300.0f);
			ImGui::SliderFloat("Texture scale (mm)", &PATCH_SCALE_MM, 4.0f, 80.0f);
			ImGui::SliderFloat("Flow speed", &PATCH_FLOW, 0.0f, 1.0f);
			ImGui::SliderFloat("Contrast", &PATCH_CONTRAST, 0.5f, 4.0f);
			ImGui::SliderFloat("Warp", &WARP, 0.0f, 6.0f);
			ImGui::SliderFloat("Marbling bands", &BANDS, 1.0f, 20.0f);
			ImGui::SliderFloat("Ridge width", &RIDGE_WIDTH, 0.05f, 1.0f);
			ImGui::SliderFloat("Streak length", &STREAK, 0.0f, 1.5f);
			ImGui::SliderFloat("Flow-map cycle (s)", &FLOW_CYCLE, 0.5f, 8.0f);
			ImGui::SliderFloat("Gloss relief", &BUMP, 0.0f, 10.0f);
			ImGui::SliderFloat("Highlight strength", &SPEC_GAIN, 0.0f, 2.0f);
			ImGui::SliderFloat("Highlight tightness", &SHININESS, 2.0f, 128.0f);
			ImGui::ColorEdit3("Trough colour", &TROUGH_COLOR.r);
			ImGui::SliderFloat("Patch brightness", &PATCH_BRIGHTNESS, 0.2f, 3.0f);
			ImGui::SliderFloat("Edge cooling", &EDGE_COOL, 0.0f, 1.0f);
			ImGui::SliderFloat("Rim", &RIM_GAIN, 0.0f, 1.5f);
			static const char * names[4] = { "Fresh (cyan)", "Blue-white", "Amber", "Mature (orange)" };
			for (int i = 0; i < 4; i++) {
				ImGui::PushID(i);
				ImGui::ColorEdit3(names[i], &STOP_COLOR[i].r);
				if (i > 0 && i < 3)
					ImGui::SliderFloat("at maturity", &STOP_POS[i], 0.0f, 1.0f);
				ImGui::PopID();
			}
		}
		if (ImGui::CollapsingHeader("Hyphae", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::SliderFloat("Spawn (per mm edge per s)", &F::HYPHA_SPAWN_PER_SEC, 0.0f, 0.05f, "%.4f");
			ImGui::SliderFloat("Spawn boost while touched", &F::HYPHA_TOUCH_BOOST, 0.0f, 20.0f);
			ImGui::SliderInt("Max tips", &F::MAX_TIPS, 100, 10000);
			ImGui::SliderInt("Max segments", &F::MAX_SEGMENTS, 5000, 200000);
			ImGui::SliderFloat("Hypha speed (mm/s)", &F::HYPHA_SPEED_MM_S, 0.5f, 40.0f);
			ImGui::SliderFloat("Turn noise (deg/sqrt mm)", &F::TURN_NOISE_DEG, 0.0f, 60.0f);
			ImGui::SliderFloat("Attraction", &F::ATTRACT_WEIGHT, 0.0f, 0.5f);
			ImGui::SliderFloat("Plants in attractant", &F::ATTRACT_PLANT, 0.0f, 2.0f);
			ImGui::SliderFloat("Branching (per mm)", &F::BRANCH_PER_MM, 0.0f, 0.2f);
			ImGui::SliderInt("Max generation", &F::MAX_GENERATION, 0, 10);
			ImGui::SliderFloat("Width (px)", &F::HYPHA_WIDTH_PX, 0.3f, 5.0f);
			ImGui::SliderFloat("Finest width (px)", &F::HYPHA_MIN_WIDTH_PX, 0.2f, 3.0f);
			ImGui::SliderFloat("Tip life (s)", &F::TIP_LIFE_SEC, 1.0f, 60.0f);
			ImGui::SliderFloat("Segment (mm)", &F::SEGMENT_MM, 0.5f, 8.0f);
			ImGui::SliderFloat("Leave home (mm)", &F::HOME_MM, 0.0f, 60.0f);
			ImGui::ColorEdit3("Thread colour", &HYPHA_PALE.r);
			ImGui::ColorEdit3("Thread near mature patch", &HYPHA_WARM.r);
			ImGui::SliderFloat("Warmth gain", &WARM_GAIN, 0.0f, 20.0f);
			ImGui::SliderFloat("Thread brightness", &HYPHA_BRIGHTNESS, 0.1f, 3.0f);
			ImGui::SliderFloat("Glow radius (px)", &GLOW_RADIUS_PX, 0.0f, 8.0f);
			ImGui::SliderFloat("Glow strength", &GLOW_STRENGTH, 0.0f, 3.0f);
		}
	} else if (ImGui::CollapsingHeader("Physarum (previous)", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderInt("Agent cap", &F::MAX_AGENTS, 10000, 200000);
		ImGui::SliderFloat("Spawn per touch (/s/block)", &F::SPAWN_PER_TOUCH, 0.0f, 60.0f);
		ImGui::SliderFloat("Agent life (s)", &F::AGENT_LIFE_SEC, 2.0f, 60.0f);
		ImGui::SliderFloat("Sensor angle (deg)", &F::SENSOR_ANGLE_DEG, 5.0f, 90.0f);
		ImGui::SliderFloat("Sensor distance (mm)", &F::SENSOR_DIST_MM, 2.0f, 40.0f);
		ImGui::SliderFloat("Rotate (deg)", &F::ROTATE_DEG, 1.0f, 90.0f);
		ImGui::SliderFloat("Step (mm)", &F::STEP_MM, 0.1f, 4.0f);
		ImGui::SliderFloat("Comb weight", &F::COMB_WEIGHT, 0.0f, 1.0f);
		ImGui::SliderFloat("Curl share", &F::CURL_SHARE, 0.0f, 2.0f);
		ImGui::SliderFloat("Curl size (mm)", &F::CURL_SCALE_MM, 20.0f, 400.0f);
		ImGui::SliderFloat("Agent deposit", &F::AGENT_DEPOSIT, 0.0f, 10.0f);
		ImGui::SliderFloat("Trail decay (kept/step)", &F::TRAIL_DECAY, 0.5f, 0.99f);
		ImGui::SliderFloat("Trace deposit", &F::TRACE_DEPOSIT, 0.01f, 2.0f);
		ImGui::SliderFloat("Trace saturation", &F::TRACE_SATURATION, 5.0f, 200.0f);
		ImGui::SliderFloat("Brightness", &BRIGHTNESS, 0.2f, 3.0f);
		ImGui::SliderFloat("Gamma", &GAMMA, 0.2f, 2.5f);
		ImGui::SliderFloat("Detail (local contrast)", &DETAIL, 0.0f, 0.95f);
	}
	ImGui::Text("(keep saturated red away from saturated green)");
	ImGui::End();
}
