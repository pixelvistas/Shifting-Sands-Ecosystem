#include "AsciiRenderer.h"
#include "../../KinectProjector/KinectProjector.h"
#include "../../Games/VegetationField.h"
#include "ofxImGui.h"
#include <cstring>

// Default (DEVLOG, 2026-11-16): 10 mm cells; to be confirmed on the sand.
float AsciiRenderer::GLYPH_MM = 10.0f;

static const int LUT_STEP = 4; // grid cells between mesh vertices
static const char * PRESET_FILE = "settings/asciiPreset.xml";
static const char * PRESET_NAME = "Prototype 3: ASCII";

static void copyRamp(char * buf, const std::string & ramp)
{
	const size_t n = std::min(ramp.size(), (size_t)AsciiGlyphRenderer::MAX_RAMP);
	std::memcpy(buf, ramp.data(), n);
	buf[n] = '\0';
}

void AsciiRenderer::setup(std::shared_ptr<KinectProjector> const & kp, const VegetationField * vf)
{
	kinectProjector = kp;
	vegetation = vf;
	if (ofFile::doesFileExist(PRESET_FILE))
		loadPreset();
	copyRamp(rampBuf, AsciiGlyphRenderer::RAMP);
}

void AsciiRenderer::setProjectorRes(const ofVec2f & res)
{
	projRes = res;
	glyphs.setProjectorRes(res);
}

// Projector pixels per mm on the sand, from the calibration: the
// projector distance between the points at 1/4 and 3/4 of the play area
// (across its middle, horizontally and vertically) over their distance
// in mm (Kinect pixels x mm per Kinect pixel), averaged over both axes.
void AsciiRenderer::measureScale()
{
	const int cols = vegetation->getCols(), rows = vegetation->getRows();
	const float step = vegetation->getGridStep();
	const float mmPerKinectPx = vegetation->getMmPerCell() / std::max(1e-6f, step);
	const ofVec2f o = vegetation->getGridOrigin();
	const float w = cols * step, h = rows * step;
	if (cols < 4 || rows < 4 || mmPerKinectPx <= 0.0f)
		return;
	const ofVec2f a = kinectProjector->kinectCoordToProjCoord(o.x + 0.25f * w, o.y + 0.5f * h);
	const ofVec2f b = kinectProjector->kinectCoordToProjCoord(o.x + 0.75f * w, o.y + 0.5f * h);
	const ofVec2f c = kinectProjector->kinectCoordToProjCoord(o.x + 0.5f * w, o.y + 0.25f * h);
	const ofVec2f d = kinectProjector->kinectCoordToProjCoord(o.x + 0.5f * w, o.y + 0.75f * h);
	const float sx = a.distance(b) / (0.5f * w * mmPerKinectPx);
	const float sy = c.distance(d) / (0.5f * h * mmPerKinectPx);
	if (!(sx > 0.0f) || !(sy > 0.0f) || !std::isfinite(sx) || !std::isfinite(sy))
		return;
	pxPerMM = 0.5f * (sx + sy);
	scaleMeasured = true;
	measuredOrigin = o;
	measuredCols = cols;
	measuredRows = rows;
}

// R32F, one texel per grid cell: elevation normalized over the
// calibrated range (0..1), -1 where there is no valid sample.
void AsciiRenderer::uploadField()
{
	const uint64_t t0 = ofGetElapsedTimeMicros();
	const int cols = vegetation->getCols(), rows = vegetation->getRows();
	const cv::Mat & elev = vegetation->getElevationGrid();
	if (!fieldPix.isAllocated() || (int)fieldPix.getWidth() != cols || (int)fieldPix.getHeight() != rows)
		fieldPix.allocate(cols, rows, OF_PIXELS_GRAY);
	if (!fieldTex.isAllocated() || (int)fieldTex.getWidth() != cols || (int)fieldTex.getHeight() != rows) {
		fieldTex.allocate(cols, rows, GL_R32F, true); // rectangle texture: texel coordinates
		fieldTex.setTextureMinMagFilter(GL_NEAREST, GL_NEAREST);
		fieldTex.setTextureWrap(GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
	}
	const float ceiling = std::fabs(kinectProjector->getCalibratedCeilingElevation());
	rangeLoMM = -ceiling;
	rangeHiMM = ceiling;
	const float inv = 1.0f / std::max(1e-3f, rangeHiMM - rangeLoMM);
	float * p = fieldPix.getData();
	for (int gy = 0; gy < rows; gy++) {
		const float * e = elev.ptr<float>(gy);
		for (int gx = 0; gx < cols; gx++)
			*p++ = std::isfinite(e[gx]) ? ofClamp((e[gx] - rangeLoMM) * inv, 0.0f, 1.0f) : -1.0f;
	}
	fieldTex.loadData(fieldPix);
	lastFieldMs = (ofGetElapsedTimeMicros() - t0) / 1000.0f;
}

// Grid vertices every LUT_STEP cells at their projector positions
// (kinectCoordToProjCoord depends on the current depth, so rebuilt each
// frame). Texcoord = field texel coordinate of the cell centre.
void AsciiRenderer::rebuildMesh()
{
	const uint64_t t0 = ofGetElapsedTimeMicros();
	const int cols = vegetation->getCols(), rows = vegetation->getRows();
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
			const ofVec2f pr = kinectProjector->kinectCoordToProjCoord(origin.x + gx * step, origin.y + gy * step);
			const size_t v = (size_t)j * mw + i;
			mesh.setVertex(v, glm::vec3(pr.x, pr.y, 0));
			mesh.setTexCoord(v, glm::vec2(gx + 0.5f, gy + 0.5f));
		}
	lastMeshMs = (ofGetElapsedTimeMicros() - t0) / 1000.0f;
}

void AsciiRenderer::update()
{
	if (!kinectProjector || !vegetation || glFailed)
		return;
	const int cols = vegetation->getCols(), rows = vegetation->getRows();
	const cv::Mat & elev = vegetation->getElevationGrid();
	if (cols <= 0 || rows <= 0 || elev.rows != rows || elev.cols != cols || elev.type() != CV_32F)
		return;
	if (!glReady) {
		glReady = glyphs.setup();
		glFailed = !glReady;
		if (glFailed)
			return;
	}
	// Re-entering the mode: start from the current sand, not from old state.
	const uint64_t frame = ofGetFrameNum();
	if (frame != lastUpdateFrame + 1)
		glyphs.reset();
	lastUpdateFrame = frame;

	if (!scaleMeasured || measuredCols != cols || measuredRows != rows || measuredOrigin != vegetation->getGridOrigin())
		measureScale();
	if (!scaleMeasured)
		return;
	GLYPH_MM = ofClamp(GLYPH_MM, 2.0f, 60.0f);
	glyphs.setCellHeightPx((int)std::lround(GLYPH_MM * pxPerMM));

	uploadField();
	rebuildMesh();
	float dt = ofGetLastFrameTime();
	if (!(dt > 0.0f) || dt > 0.1f)
		dt = 1.0f / 60.0f;
	glyphs.update(fieldTex, mesh, dt);
}

void AsciiRenderer::drawMainWindow(float x, float y, float w, float h) { glyphs.draw(x, y, w, h); }
void AsciiRenderer::drawProjectorWindow() { glyphs.draw(0, 0, projRes.x, projRes.y); }

// ------------------------------------------------------------ preset
// The ramp is stored as character codes: leading spaces survive.
bool AsciiRenderer::savePreset()
{
	ofXml xml;
	auto s = xml.appendChild("ASCIIPRESET");
	s.appendChild("name").set(std::string(PRESET_NAME));
	s.appendChild("GLYPH_MM").set(GLYPH_MM);
	std::string codes;
	for (size_t i = 0; i < AsciiGlyphRenderer::RAMP.size(); i++)
		codes += (i ? "," : "") + ofToString((int)(unsigned char)AsciiGlyphRenderer::RAMP[i]);
	s.appendChild("RAMP_CODES").set(codes);
	s.appendChild("SMOOTHING_SEC").set(AsciiGlyphRenderer::SMOOTHING_SEC);
	s.appendChild("HYSTERESIS_STEPS").set(AsciiGlyphRenderer::HYSTERESIS_STEPS);
	s.appendChild("DITHER").set(AsciiGlyphRenderer::DITHER ? 1 : 0);
	s.appendChild("BRIGHTNESS").set(AsciiGlyphRenderer::BRIGHTNESS);
	const bool ok = xml.save(PRESET_FILE);
	presetStatus = ok ? std::string("saved \"") + PRESET_NAME + "\"" : "save failed";
	return ok;
}

// Reads only the entries present, so an older file keeps newer defaults.
bool AsciiRenderer::loadPreset()
{
	ofXml xml;
	if (!xml.load(PRESET_FILE)) { presetStatus = "no preset file"; return false; }
	auto s = xml.getChild("ASCIIPRESET");
	if (!s) { presetStatus = "preset file not recognised"; return false; }
	if (s.getChild("GLYPH_MM")) GLYPH_MM = s.getChild("GLYPH_MM").getFloatValue();
	if (s.getChild("RAMP_CODES")) {
		std::string ramp;
		for (const std::string & c : ofSplitString(s.getChild("RAMP_CODES").getValue(), ",", true, true)) {
			const int code = ofToInt(c);
			if (code >= 32 && code <= 126)
				ramp.push_back((char)code);
		}
		if (ramp.size() >= 2 && ramp.size() <= (size_t)AsciiGlyphRenderer::MAX_RAMP)
			AsciiGlyphRenderer::RAMP = ramp;
	}
	if (s.getChild("SMOOTHING_SEC")) AsciiGlyphRenderer::SMOOTHING_SEC = s.getChild("SMOOTHING_SEC").getFloatValue();
	if (s.getChild("HYSTERESIS_STEPS")) AsciiGlyphRenderer::HYSTERESIS_STEPS = s.getChild("HYSTERESIS_STEPS").getFloatValue();
	if (s.getChild("DITHER")) AsciiGlyphRenderer::DITHER = s.getChild("DITHER").getIntValue() != 0;
	if (s.getChild("BRIGHTNESS")) AsciiGlyphRenderer::BRIGHTNESS = s.getChild("BRIGHTNESS").getFloatValue();
	copyRamp(rampBuf, AsciiGlyphRenderer::RAMP);
	presetStatus = std::string("loaded \"") + s.getChild("name").getValue() + "\"";
	return true;
}

// ------------------------------------------------------------ GUI
void AsciiRenderer::drawGui()
{
	typedef AsciiGlyphRenderer G;
	ImGui::Begin("ASCII");
	ImGui::Text("Prototype 3: the sand's elevation as characters.");
	ImGui::Text("Display only: water and succession keep running unseen.");
	if (glFailed)
		ImGui::Text("Shaders or font not loaded (bin/data/shaders/ascii, bin/data/ascii/fonts).");
	if (ImGui::Button("Save preset"))
		savePreset();
	ImGui::SameLine();
	if (ImGui::Button("Load preset"))
		loadPreset();
	if (!presetStatus.empty())
		ImGui::Text("Preset: %s", presetStatus.c_str());

	ImGui::Separator();
	ImGui::SliderFloat("Glyph size (mm)", &GLYPH_MM, 2.0f, 60.0f);
	if (scaleMeasured)
		ImGui::Text("%.2f projector px/mm: cell %d x %d px (font %d px), grid %d x %d",
			pxPerMM, glyphs.getCellWidth(), glyphs.getCellHeight(), glyphs.getFontSize(), glyphs.getCols(), glyphs.getRows());
	else
		ImGui::Text("Scale not measured yet (needs the calibrated play area).");
	if (ImGui::Button("Re-measure scale"))
		scaleMeasured = false;

	if (ImGui::InputText("Glyph ramp", rampBuf, sizeof(rampBuf))) {
		std::string r(rampBuf);
		if (r.size() >= 2)
			G::RAMP = r;
	}
	ImGui::Text("(lightest to densest ink, 2..%d characters; %d in use)", G::MAX_RAMP, glyphs.getLevels());
	ImGui::SliderFloat("Smoothing time (s)", &G::SMOOTHING_SEC, 0.0f, 3.0f);
	ImGui::SliderFloat("Hysteresis (ramp steps)", &G::HYSTERESIS_STEPS, 0.0f, 0.5f);
	const float stepMM = (rangeHiMM - rangeLoMM) / std::max(1, glyphs.getLevels() - 1);
	ImGui::Text("(one step = %.1f mm of elevation; hysteresis = %.1f mm)", stepMM, stepMM * G::HYSTERESIS_STEPS);
	ImGui::Checkbox("Dithering (4x4 Bayer across cells)", &G::DITHER);
	ImGui::SliderFloat("Brightness", &G::BRIGHTNESS, 0.0f, 1.0f);

	ImGui::Separator();
	ImGui::Text("Elevation range (calibrated): %.1f .. %.1f mm", rangeLoMM, rangeHiMM);
	ImGui::Text("fps %.1f; field %.2f ms, mesh %.2f ms", ofGetFrameRate(), lastFieldMs, lastMeshMs);
	ImGui::End();
}
