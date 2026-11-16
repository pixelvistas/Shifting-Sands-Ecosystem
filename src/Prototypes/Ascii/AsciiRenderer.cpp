#include "AsciiRenderer.h"
#include "../../KinectProjector/KinectProjector.h"
#include "../../Games/VegetationField.h"
#include "ofxImGui.h"
#include <cstring>

// Default (DEVLOG, 2026-11-16): 10 mm cells; to be confirmed on the sand.
float AsciiRenderer::GLYPH_MM = 10.0f;
int AsciiRenderer::RANGE_MODE = AsciiRenderer::RANGE_CALIBRATED;
float AsciiRenderer::RANGE_MIN_MM = -80.0f; // Custom; reset button: -/+ calibrated ceiling
float AsciiRenderer::RANGE_MAX_MM = 80.0f;

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

// The active range, sorted. A range under 1 mm wide is widened to 1 mm
// around its centre (and flagged in the panel) so the division stays finite.
void AsciiRenderer::updateRange()
{
	float a = 0.0f, b = 0.0f;
	if (RANGE_MODE == RANGE_VEGETATION && vegetation) {
		vegetation->getElevationRange(a, b);
	} else if (RANGE_MODE == RANGE_CUSTOM) {
		a = RANGE_MIN_MM;
		b = RANGE_MAX_MM;
	} else {
		const float ceiling = kinectProjector ? std::fabs(kinectProjector->getCalibratedCeilingElevation()) : 0.0f;
		a = -ceiling;
		b = ceiling;
	}
	rangeLoMM = std::min(a, b);
	rangeHiMM = std::max(a, b);
	rangeDegenerate = !(rangeHiMM - rangeLoMM >= 1.0f);
	if (rangeDegenerate) {
		const float c = std::isfinite(a + b) ? 0.5f * (a + b) : 0.0f;
		rangeLoMM = c - 0.5f;
		rangeHiMM = c + 0.5f;
	}
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

	updateRange();
	uploadField();
	rebuildMesh();
	float dt = ofGetLastFrameTime();
	if (!(dt > 0.0f) || dt > 0.1f)
		dt = 1.0f / 60.0f;
	glyphs.setFieldUnits(rangeLoMM, rangeHiMM - rangeLoMM);
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
	s.appendChild("RANGE_MODE").set(RANGE_MODE);
	s.appendChild("RANGE_MIN_MM").set(RANGE_MIN_MM);
	s.appendChild("RANGE_MAX_MM").set(RANGE_MAX_MM);
	std::string codes;
	for (size_t i = 0; i < AsciiGlyphRenderer::RAMP.size(); i++)
		codes += (i ? "," : "") + ofToString((int)(unsigned char)AsciiGlyphRenderer::RAMP[i]);
	s.appendChild("RAMP_CODES").set(codes);
	s.appendChild("SMOOTHING_SEC").set(AsciiGlyphRenderer::SMOOTHING_SEC);
	s.appendChild("HYSTERESIS_STEPS").set(AsciiGlyphRenderer::HYSTERESIS_STEPS);
	s.appendChild("DITHER").set(AsciiGlyphRenderer::DITHER ? 1 : 0);
	s.appendChild("BRIGHTNESS").set(AsciiGlyphRenderer::BRIGHTNESS);
	s.appendChild("SHOW_CONTOURS").set(AsciiGlyphRenderer::SHOW_CONTOURS ? 1 : 0);
	s.appendChild("CONTOUR_INTERVAL_MM").set(AsciiGlyphRenderer::CONTOUR_INTERVAL);
	s.appendChild("CONTOUR_OFFSET_MM").set(AsciiGlyphRenderer::CONTOUR_OFFSET);
	s.appendChild("CONTOUR_BRIGHTEN").set(AsciiGlyphRenderer::CONTOUR_BRIGHTEN);
	for (int b = 0; b < AsciiGlyphRenderer::BANDS; b++) {
		auto c = s.appendChild("band" + ofToString(b));
		c.appendChild("fg").set(ofToString(AsciiGlyphRenderer::FG_COLOR[b].r) + "," + ofToString(AsciiGlyphRenderer::FG_COLOR[b].g)
			+ "," + ofToString(AsciiGlyphRenderer::FG_COLOR[b].b));
		c.appendChild("bg").set(ofToString(AsciiGlyphRenderer::BG_COLOR[b].r) + "," + ofToString(AsciiGlyphRenderer::BG_COLOR[b].g)
			+ "," + ofToString(AsciiGlyphRenderer::BG_COLOR[b].b));
	}
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
	if (s.getChild("RANGE_MODE")) RANGE_MODE = std::max(0, std::min(2, s.getChild("RANGE_MODE").getIntValue()));
	if (s.getChild("RANGE_MIN_MM")) RANGE_MIN_MM = s.getChild("RANGE_MIN_MM").getFloatValue();
	if (s.getChild("RANGE_MAX_MM")) RANGE_MAX_MM = s.getChild("RANGE_MAX_MM").getFloatValue();
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
	if (s.getChild("SHOW_CONTOURS")) AsciiGlyphRenderer::SHOW_CONTOURS = s.getChild("SHOW_CONTOURS").getIntValue() != 0;
	if (s.getChild("CONTOUR_INTERVAL_MM")) AsciiGlyphRenderer::CONTOUR_INTERVAL = s.getChild("CONTOUR_INTERVAL_MM").getFloatValue();
	if (s.getChild("CONTOUR_OFFSET_MM")) AsciiGlyphRenderer::CONTOUR_OFFSET = s.getChild("CONTOUR_OFFSET_MM").getFloatValue();
	if (s.getChild("CONTOUR_BRIGHTEN")) AsciiGlyphRenderer::CONTOUR_BRIGHTEN = s.getChild("CONTOUR_BRIGHTEN").getFloatValue();
	for (int b = 0; b < AsciiGlyphRenderer::BANDS; b++) {
		auto c = s.getChild("band" + ofToString(b));
		if (!c)
			continue;
		for (int k = 0; k < 2; k++) {
			auto e = c.getChild(k == 0 ? "fg" : "bg");
			const std::vector<std::string> v = e ? ofSplitString(e.getValue(), ",", true, true) : std::vector<std::string>();
			if (v.size() != 3)
				continue;
			ofFloatColor & col = (k == 0 ? AsciiGlyphRenderer::FG_COLOR : AsciiGlyphRenderer::BG_COLOR)[b];
			col.r = ofClamp(ofToFloat(v[0]), 0.0f, 1.0f);
			col.g = ofClamp(ofToFloat(v[1]), 0.0f, 1.0f);
			col.b = ofClamp(ofToFloat(v[2]), 0.0f, 1.0f);
		}
	}
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
	static const char * rangeModes[] = { "Calibrated ceiling", "Match the Vegetation panel", "Custom" };
	ImGui::Combo("Elevation range", &RANGE_MODE, rangeModes, 3);
	if (RANGE_MODE == RANGE_CUSTOM) {
		ImGui::SliderFloat("Range min (mm)", &RANGE_MIN_MM, -300.0f, 300.0f);
		ImGui::SliderFloat("Range max (mm)", &RANGE_MAX_MM, -300.0f, 300.0f);
		if (ImGui::Button("Reset to +-calibrated ceiling") && kinectProjector) {
			const float ceiling = std::fabs(kinectProjector->getCalibratedCeilingElevation());
			RANGE_MIN_MM = -ceiling;
			RANGE_MAX_MM = ceiling;
		}
	}
	updateRange(); // show the current setting even before the next update()
	ImGui::Text("Active range: %.1f .. %.1f mm (%s)", rangeLoMM, rangeHiMM, rangeModes[std::max(0, std::min(2, RANGE_MODE))]);
	if (rangeDegenerate)
		ImGui::Text("Range under 1 mm wide: widened to 1 mm around its centre.");

	ImGui::Separator();
	ImGui::Text("Height bands (5, dithered across cells with the same Bayer pattern)");
	static const char * bandNames[G::BANDS] = { "Low", "Low-mid", "Mid", "High-mid", "High" };
	const float bandStepMM = (rangeHiMM - rangeLoMM) / (G::BANDS - 1);
	for (int b = 0; b < G::BANDS; b++) {
		ImGui::PushID(b);
		ImGui::Text("%-8s centre %6.1f mm", bandNames[b], rangeLoMM + b * bandStepMM);
		ImGui::ColorEdit3("glyph", &G::FG_COLOR[b].r);
		ImGui::SameLine();
		ImGui::ColorEdit3("background", &G::BG_COLOR[b].r);
		ImGui::PopID();
	}
	if (ImGui::Button("Reset palettes")) {
		for (int b = 0; b < G::BANDS; b++) {
			G::FG_COLOR[b] = G::DEFAULT_FG_COLOR[b];
			G::BG_COLOR[b] = G::DEFAULT_BG_COLOR[b];
		}
	}
	ImGui::Text("(defaults checked for colour-blind contrast; keep saturated red away from saturated green)");
	ImGui::Checkbox("Contours", &G::SHOW_CONTOURS);
	ImGui::SliderFloat("Contour interval (mm)", &G::CONTOUR_INTERVAL, 2.0f, 50.0f);
	ImGui::SliderFloat("Contour offset (mm)", &G::CONTOUR_OFFSET, 0.0f, 50.0f);
	ImGui::SliderFloat("Contour glyph brighten", &G::CONTOUR_BRIGHTEN, 0.0f, 1.0f);
	ImGui::Text("(contours at offset + k x interval; hysteresis %.1f mm)", G::HYSTERESIS_STEPS * G::CONTOUR_INTERVAL);

	ImGui::Separator();
	ImGui::Text("fps %.1f; field %.2f ms, mesh %.2f ms", ofGetFrameRate(), lastFieldMs, lastMeshMs);
	ImGui::End();
}
