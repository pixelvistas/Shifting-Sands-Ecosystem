#include "AsciiRenderer.h"
#include "AsciiFont.h"
#include "../../KinectProjector/KinectProjector.h"
#include "../../Games/VegetationField.h"
#include "ofxImGui.h"

// Display defaults (not yet tuned on the sandbox). Land range = the
// colour map's key range (-45..85 mm); no saturated red anywhere.
int AsciiRenderer::CELL_PX = 12;
float AsciiRenderer::LAND_MIN_MM = -45.0f;
float AsciiRenderer::LAND_MAX_MM = 85.0f;
bool AsciiRenderer::SHOW_CONTOURS = true;
float AsciiRenderer::CONTOUR_INTERVAL_MM = 10.0f;
float AsciiRenderer::CONTOUR_OFFSET_MM = 5.0f; // contours at 5, 15, 25 mm: flat sand (~9 mm) sits between two
bool AsciiRenderer::CONTOURS_OVER_PLANTS = true;
bool AsciiRenderer::SHOW_PLANTS = true;
float AsciiRenderer::PLANT_MIN_COVER = 0.15f;
float AsciiRenderer::WATER_MIN_FRACTION = 0.5f;
float AsciiRenderer::WATER_FULL_MM = 20.0f;
float AsciiRenderer::WAVE_SPEED = 0.4f;
float AsciiRenderer::WAVE_LENGTH_CELLS = 6.0f;
float AsciiRenderer::LAND_DIM = 0.35f;
float AsciiRenderer::BRIGHTNESS = 1.0f;
ofFloatColor AsciiRenderer::LAND_COLOR(0.80f, 0.72f, 0.56f);    // sand
ofFloatColor AsciiRenderer::CONTOUR_COLOR(1.00f, 0.86f, 0.60f); // warm highlight
ofFloatColor AsciiRenderer::WATER_COLOR(0.44f, 0.83f, 1.00f);   // #6FD3FF, as the water lines
ofFloatColor AsciiRenderer::PLANT_COLOR[3] = {
	ofFloatColor(0.86f, 0.92f, 0.45f),   // pioneer: pale yellow-green
	ofFloatColor(0.45f, 0.86f, 0.40f),   // intermediate: green
	ofFloatColor(0.20f, 0.68f, 0.50f) }; // climax: deep teal-green
ofFloatColor AsciiRenderer::SNOW_COLOR(0.96f, 0.96f, 0.95f);

// Glyph sets, in the order the shader indexes them.
static const char LAND_GLYPHS[8] = { ' ', '.', '.', ':', ':', 'o', 'O', '@' }; // flat sand (~9 mm) is ':'
static const char PLANT_GLYPHS[9] = { ',', ';', 'v',   '+', 'x', 'X',   '%', '&', '#' };
static const char WATER_GLYPHS[4] = { '~', '-', '~', '=' };
static const char CONTOUR_GLYPHS[4] = { '-', '\\', '|', '/' }; // tangent 0, 45, 90, 135 deg (y down)
static const char SNOW_GLYPH = '*';

static const int LUT_STEP = 4; // grid cells between mesh vertices

void AsciiRenderer::setup(std::shared_ptr<KinectProjector> const & kp, const VegetationField * vf)
{
	kinectProjector = kp;
	vegetation = vf;
#ifdef TARGET_OPENGLES
	shadersLoaded = false;
#else
	const std::string dir = ofIsGLProgrammableRenderer() ? "shaders/ascii/GL3/" : "shaders/ascii/GL2/";
	shadersLoaded = cellShader.load(dir + "asciiCells") && asciiShader.load(dir + "ascii");
#endif
	if (!shadersLoaded)
		ofLogError("AsciiRenderer") << "setup(): could not load shaders/ascii";

	std::vector<unsigned char> atlas;
	int aw = 0, ah = 0;
	AsciiFont::buildAtlas(atlas, aw, ah);
	ofPixels atlasPix;
	atlasPix.allocate(aw, ah, OF_PIXELS_RGBA);
	for (int i = 0; i < aw * ah; i++) {
		unsigned char * p = atlasPix.getData() + 4 * i;
		p[0] = p[1] = p[2] = atlas[i];
		p[3] = 255;
	}
	glyphTex.allocate(aw, ah, GL_RGBA8, true); // rectangle texture: texel coordinates
	glyphTex.setTextureMinMagFilter(GL_NEAREST, GL_NEAREST);
	glyphTex.setTextureWrap(GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
	glyphTex.loadData(atlasPix);
}

void AsciiRenderer::setProjectorRes(const ofVec2f & res)
{
	projRes = res;
	fbo.allocate(std::max(1, (int)projRes.x), std::max(1, (int)projRes.y), GL_RGBA);
	fbo.begin();
	ofClear(0, 0, 0, 255);
	fbo.end();
	quad.clear();
	quad.setMode(OF_PRIMITIVE_TRIANGLE_STRIP);
	const float w = projRes.x, h = projRes.y;
	quad.addVertex(glm::vec3(0, 0, 0)); quad.addTexCoord(glm::vec2(0, 0));
	quad.addVertex(glm::vec3(w, 0, 0)); quad.addTexCoord(glm::vec2(w, 0));
	quad.addVertex(glm::vec3(0, h, 0)); quad.addTexCoord(glm::vec2(0, h));
	quad.addVertex(glm::vec3(w, h, 0)); quad.addTexCoord(glm::vec2(w, h));
	allocatedCellPx = 0; // reallocate the cell FBO for the new size
}

void AsciiRenderer::allocateCells()
{
	CELL_PX = std::max(4, std::min(64, CELL_PX));
	const int c = std::max(1, (int)std::ceil(projRes.x / CELL_PX));
	const int r = std::max(1, (int)std::ceil(projRes.y / CELL_PX));
	if (allocatedCellPx == CELL_PX && c == cellCols && r == cellRows && cellFbo.isAllocated())
		return;
	ofFboSettings s;
	s.width = c;
	s.height = r;
	s.internalformat = GL_RGBA32F;
	s.textureTarget = GL_TEXTURE_RECTANGLE_ARB;
	s.minFilter = GL_NEAREST;
	s.maxFilter = GL_NEAREST;
	s.wrapModeHorizontal = GL_CLAMP_TO_EDGE;
	s.wrapModeVertical = GL_CLAMP_TO_EDGE;
	s.useDepth = false;
	cellFbo.allocate(s);
	cellCols = c;
	cellRows = r;
	allocatedCellPx = CELL_PX;
}

// R = elevation (mm), G = water depth (mm) / -1 snow / 0 dry,
// B = dominant role x 2 + its cover, or -1 (no plants), A = 1 valid / 0 not.
void AsciiRenderer::uploadData()
{
	const uint64_t t0 = ofGetElapsedTimeMicros();
	const int cols = vegetation->getCols(), rows = vegetation->getRows();
	const cv::Mat & elev = vegetation->getElevationGrid();
	if (!dataPix.isAllocated() || (int)dataPix.getWidth() != cols || (int)dataPix.getHeight() != rows)
		dataPix.allocate(cols, rows, OF_PIXELS_RGBA);
	if (!dataTex.isAllocated() || (int)dataTex.getWidth() != cols || (int)dataTex.getHeight() != rows) {
		dataTex.allocate(cols, rows, GL_RGBA32F, true);
		dataTex.setTextureMinMagFilter(GL_NEAREST, GL_NEAREST);
		dataTex.setTextureWrap(GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
	}
	const SuccessionModel & sm = vegetation->getSuccession();
	const bool plants = SHOW_PLANTS && VegetationField::VEGETATION_MODEL == VegetationField::MODEL_SITE_FEEDBACK
		&& sm.getCols() == cols && sm.getRows() == rows;
	const float * cover[3] = { nullptr, nullptr, nullptr };
	if (plants)
		for (int r = 0; r < 3; r++)
			cover[r] = sm.cover(r);
	const bool snowPossible = VegetationField::VEGETATION_MODEL == VegetationField::MODEL_ELF;
	const ofVec2f origin = vegetation->getGridOrigin();
	const float step = vegetation->getGridStep();

	float * p = dataPix.getData();
	int nWater = 0, nPlant = 0;
	for (int gy = 0; gy < rows; gy++) {
		const float * e = elev.ptr<float>(gy);
		for (int gx = 0; gx < cols; gx++, p += 4) {
			const size_t i = (size_t)gy * cols + gx;
			const float h = e[gx];
			if (!std::isfinite(h)) { p[0] = p[1] = 0.0f; p[2] = -1.0f; p[3] = 0.0f; continue; }
			p[0] = h;
			p[1] = 0.0f;
			if (vegetation->isWaterCell(gx, gy)) {
				p[1] = std::max(1e-3f, vegetation->getWaterDepthAt(gx, gy));
				nWater++;
			} else if (snowPossible && vegetation->isSnowAt(origin.x + gx * step, origin.y + gy * step)) {
				p[1] = -1.0f;
			}
			p[2] = -1.0f;
			if (plants) {
				int best = 0;
				for (int r = 1; r < 3; r++)
					if (cover[r][i] > cover[best][i]) best = r;
				const float c = std::min(1.0f, std::max(0.0f, cover[best][i]));
				if (c >= PLANT_MIN_COVER) {
					p[2] = 2.0f * best + c;
					nPlant++;
				}
			}
			p[3] = 1.0f;
		}
	}
	dataTex.loadData(dataPix);
	waterCells = nWater;
	plantCells = nPlant;
	lastUploadMs = (ofGetElapsedTimeMicros() - t0) / 1000.0f;
}

// Grid vertices every LUT_STEP cells at their projector positions
// (kinectCoordToProjCoord depends on the current depth, so rebuilt each
// frame). Texcoord = data texel coordinate of the cell centre.
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

void AsciiRenderer::renderFbo()
{
	if (!fbo.isAllocated() || !cellFbo.isAllocated())
		return;
	ofPushStyle();
	ofEnableBlendMode(OF_BLENDMODE_DISABLED);
	ofSetColor(255);

	// Pass 1: one aggregated data texel per character cell.
	cellFbo.begin();
	ofClear(0, 0, 0, 0);
	cellShader.begin();
	cellShader.setUniformTexture("dataTex", dataTex, 0);
	cellShader.setUniform1f("waterMinFraction", WATER_MIN_FRACTION);
	ofPushMatrix();
	ofScale(1.0f / CELL_PX, 1.0f / CELL_PX);
	mesh.draw();
	ofPopMatrix();
	cellShader.end();
	cellFbo.end();

	// Pass 2: glyphs at projector resolution.
	float land[8], plant[9], water[4], contour[4];
	for (int i = 0; i < 8; i++) land[i] = (float)AsciiFont::indexOf(LAND_GLYPHS[i]);
	for (int i = 0; i < 9; i++) plant[i] = (float)AsciiFont::indexOf(PLANT_GLYPHS[i]);
	for (int i = 0; i < 4; i++) water[i] = (float)AsciiFont::indexOf(WATER_GLYPHS[i]);
	for (int i = 0; i < 4; i++) contour[i] = (float)AsciiFont::indexOf(CONTOUR_GLYPHS[i]);
	float plantCol[9];
	for (int r = 0; r < 3; r++) {
		plantCol[3 * r + 0] = PLANT_COLOR[r].r;
		plantCol[3 * r + 1] = PLANT_COLOR[r].g;
		plantCol[3 * r + 2] = PLANT_COLOR[r].b;
	}
	fbo.begin();
	ofClear(0, 0, 0, 255);
	asciiShader.begin();
	asciiShader.setUniformTexture("cellTex", cellFbo.getTexture(), 0);
	asciiShader.setUniformTexture("glyphTex", glyphTex, 1);
	asciiShader.setUniform1f("cellPx", (float)CELL_PX);
	asciiShader.setUniform2f("cellGrid", (float)cellCols, (float)cellRows);
	asciiShader.setUniform1f("glyphPx", (float)AsciiFont::GLYPH_PX);
	asciiShader.setUniform1fv("landGlyph", land, 8);
	asciiShader.setUniform1fv("plantGlyph", plant, 9);
	asciiShader.setUniform1fv("waterGlyph", water, 4);
	asciiShader.setUniform1fv("contourGlyph", contour, 4);
	asciiShader.setUniform1f("snowGlyph", (float)AsciiFont::indexOf(SNOW_GLYPH));
	asciiShader.setUniform1f("landMinMM", LAND_MIN_MM);
	asciiShader.setUniform1f("landMaxMM", std::max(LAND_MIN_MM + 1.0f, LAND_MAX_MM));
	asciiShader.setUniform1i("showContours", SHOW_CONTOURS ? 1 : 0);
	asciiShader.setUniform1f("contourIntervalMM", std::max(1.0f, CONTOUR_INTERVAL_MM));
	asciiShader.setUniform1f("contourOffsetMM", CONTOUR_OFFSET_MM);
	asciiShader.setUniform1i("contoursOverPlants", CONTOURS_OVER_PLANTS ? 1 : 0);
	asciiShader.setUniform1f("plantMinCover", PLANT_MIN_COVER);
	asciiShader.setUniform1f("waterFullMM", std::max(0.5f, WATER_FULL_MM));
	asciiShader.setUniform1f("wavePhase", ofGetElapsedTimef() * WAVE_SPEED);
	asciiShader.setUniform1f("waveLengthCells", std::max(1.0f, WAVE_LENGTH_CELLS));
	asciiShader.setUniform1f("landDim", LAND_DIM);
	asciiShader.setUniform1f("brightness", BRIGHTNESS);
	asciiShader.setUniform3f("landColor", LAND_COLOR.r, LAND_COLOR.g, LAND_COLOR.b);
	asciiShader.setUniform3f("contourColor", CONTOUR_COLOR.r, CONTOUR_COLOR.g, CONTOUR_COLOR.b);
	asciiShader.setUniform3f("waterColor", WATER_COLOR.r, WATER_COLOR.g, WATER_COLOR.b);
	asciiShader.setUniform3fv("plantColor", plantCol, 3);
	asciiShader.setUniform3f("snowColor", SNOW_COLOR.r, SNOW_COLOR.g, SNOW_COLOR.b);
	quad.draw();
	asciiShader.end();
	fbo.end();

	ofPopStyle();
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
}

void AsciiRenderer::update()
{
	if (!kinectProjector || !vegetation || !shadersLoaded)
		return;
	const int cols = vegetation->getCols(), rows = vegetation->getRows();
	const cv::Mat & elev = vegetation->getElevationGrid();
	if (cols <= 0 || rows <= 0 || elev.rows != rows || elev.cols != cols || elev.type() != CV_32F)
		return;
	allocateCells();
	uploadData();
	rebuildMesh();
	renderFbo();
	hasFrame = true;
}

void AsciiRenderer::composite(float x, float y, float w, float h)
{
	if (!fbo.isAllocated())
		return;
	ofPushStyle();
	ofSetColor(255);
	ofEnableBlendMode(OF_BLENDMODE_DISABLED);
	if (hasFrame) {
		fbo.draw(x, y, w, h);
	} else {
		ofSetColor(0);
		ofDrawRectangle(x, y, w, h);
	}
	ofPopStyle();
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
}

void AsciiRenderer::drawMainWindow(float x, float y, float w, float h) { composite(x, y, w, h); }
void AsciiRenderer::drawProjectorWindow() { composite(0, 0, projRes.x, projRes.y); }

void AsciiRenderer::drawGui()
{
	ImGui::Begin("ASCII");
	ImGui::Text("Prototype 3: the sandbox drawn as text characters.");
	ImGui::Text("Display only: reads water and succession, never writes them.");
	if (!shadersLoaded)
		ImGui::Text("Shaders not loaded (bin/data/shaders/ascii): nothing is drawn.");
	ImGui::Text("Grid %d x %d characters; water cells %d, plant cells %d", cellCols, cellRows, waterCells, plantCells);
	ImGui::Text("Upload %.2f ms, mesh %.2f ms", lastUploadMs, lastMeshMs);
	if (VegetationField::VEGETATION_MODEL != VegetationField::MODEL_SITE_FEEDBACK)
		ImGui::Text("Plants are drawn for the Site feedback model only.");

	if (ImGui::CollapsingHeader("Grid", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderInt("Cell size (projector px)", &CELL_PX, 4, 64);
		ImGui::SliderFloat("Brightness", &BRIGHTNESS, 0.2f, 3.0f);
	}
	if (ImGui::CollapsingHeader("Land and contours", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderFloat("Land glyph low (mm)", &LAND_MIN_MM, -100.0f, 100.0f);
		ImGui::SliderFloat("Land glyph high (mm)", &LAND_MAX_MM, -100.0f, 150.0f);
		ImGui::SliderFloat("Lowest land brightness", &LAND_DIM, 0.0f, 1.0f);
		ImGui::Checkbox("Contours", &SHOW_CONTOURS);
		ImGui::SliderFloat("Contour interval (mm)", &CONTOUR_INTERVAL_MM, 2.0f, 40.0f);
		ImGui::SliderFloat("Contour offset (mm)", &CONTOUR_OFFSET_MM, 0.0f, 40.0f);
		ImGui::Checkbox("Contours over plants", &CONTOURS_OVER_PLANTS);
		ImGui::ColorEdit3("Land colour", &LAND_COLOR.r);
		ImGui::ColorEdit3("Contour colour", &CONTOUR_COLOR.r);
	}
	if (ImGui::CollapsingHeader("Water", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderFloat("Water share of a cell", &WATER_MIN_FRACTION, 0.1f, 1.0f);
		ImGui::SliderFloat("Full brightness depth (mm)", &WATER_FULL_MM, 1.0f, 60.0f);
		ImGui::SliderFloat("Wave speed (/s)", &WAVE_SPEED, 0.0f, 3.0f);
		ImGui::SliderFloat("Wave length (cells)", &WAVE_LENGTH_CELLS, 1.0f, 20.0f);
		ImGui::ColorEdit3("Water colour", &WATER_COLOR.r);
	}
	if (ImGui::CollapsingHeader("Plants", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Plants", &SHOW_PLANTS);
		ImGui::SliderFloat("Minimum cover", &PLANT_MIN_COVER, 0.01f, 0.9f);
		ImGui::ColorEdit3("Pioneer , ; v", &PLANT_COLOR[0].r);
		ImGui::ColorEdit3("Intermediate + x X", &PLANT_COLOR[1].r);
		ImGui::ColorEdit3("Climax % & #", &PLANT_COLOR[2].r);
		ImGui::ColorEdit3("Snow *", &SNOW_COLOR.r);
		ImGui::Text("(keep saturated red away from saturated green)");
	}
	ImGui::End();
}
