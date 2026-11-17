#include "SceneStylizer.h"
#include "SceneGL.h"

static ofFloatColor hexColor(unsigned int h)
{
	return ofFloatColor(((h >> 16) & 0xff) / 255.0f, ((h >> 8) & 0xff) / 255.0f, (h & 0xff) / 255.0f);
}

// Defaults (DEVLOG, 2026-11-18; chosen on the headless preview, to be set
// on the sand).
int SceneStylizer::STYLE = SceneStylizer::STYLE_ASCII;
float SceneStylizer::CELL_MM = 8.0f;
float SceneStylizer::LUMINANCE_GAMMA = 0.55f;
float SceneStylizer::COLOUR_BOOST = 2.0f;
std::string SceneStylizer::RAMP = " .`',:;-~^\"=+<!ivxzcoa*#%&8@$WM";
int SceneStylizer::DOT_SIZE = 2;
int SceneStylizer::LEVELS = 3;
bool SceneStylizer::BAYER8 = false;
bool SceneStylizer::PALETTE_ON = true;
int SceneStylizer::PALETTE_PRESET = SceneStylizer::PALETTE_GREEN;
int SceneStylizer::PALETTE_STOPS = 3;
ofFloatColor SceneStylizer::PALETTE[3] = { hexColor(0x020806), hexColor(0x2F7A55), hexColor(0xCFE8D2) };
float SceneStylizer::BOTH_THRESHOLD = 0.25f;
float SceneStylizer::EDGE_THRESHOLD = 0.05f;
std::string SceneStylizer::DIR_GLYPHS = "-=\\\\!|//";
float SceneStylizer::WEIGHT_SPLIT = 0.55f;
bool SceneStylizer::DEBUG_EDGES = false;
bool SceneStylizer::LINE_SCREEN = false;
int SceneStylizer::LINE_PERIOD = 2;

// Presets approximating the reference's three looks (dark, mid, light).
void SceneStylizer::applyPalettePreset(int preset)
{
	static const unsigned int P[3][3] = {
		{ 0x1A1206, 0x9C7A2E, 0xF2D58A },   // Gold
		{ 0x05070D, 0x2E4466, 0xC9D6E8 },   // Night
		{ 0x020806, 0x2F7A55, 0xCFE8D2 } }; // Green: near black, mid green, pale green
	PALETTE_PRESET = std::max(0, std::min(2, preset));
	for (int i = 0; i < 3; i++)
		PALETTE[i] = hexColor(P[PALETTE_PRESET][i]);
}

bool SceneStylizer::setup()
{
	loaded = SceneGL::load(cellShader, "sceneCells") && SceneGL::load(edgeShader, "sceneEdges")
		&& SceneGL::load(stylizeShader, "sceneStylize");
	return loaded;
}

void SceneStylizer::setProjectorRes(const ofVec2f & res)
{
	projRes = res;
	projQuad = SceneGL::quad(res.x, res.y);
	cols = rows = 0;
}

void SceneStylizer::setCellHeightPx(int px)
{
	if (!atlas.build(std::max(4, px)))
		return;
	const int c = (int)std::ceil(projRes.x / std::max(1, atlas.getCellWidth()));
	const int r = (int)std::ceil(projRes.y / std::max(1, atlas.getCellHeight()));
	if (c != cols || r != rows || !cellFbo.isAllocated()) {
		cols = c;
		rows = r;
		SceneGL::allocate(cellFbo, cols, rows, GL_RGBA16F, GL_NEAREST);
		SceneGL::allocate(edgeFbo, cols, rows, GL_RGBA16F, GL_NEAREST);
		cellQuad = SceneGL::quad((float)cols, (float)rows);
	}
	if (!outFbo.isAllocated() || (int)outFbo.getWidth() != (int)projRes.x || (int)outFbo.getHeight() != (int)projRes.y)
		SceneGL::allocate(outFbo, (int)projRes.x, (int)projRes.y, GL_RGBA8, GL_NEAREST);
	if (sortedForCell != atlas.getCellHeight() || sortedFrom != RAMP)
		sortRamp();
}

// Ink coverage of every atlas glyph (share of its cell's pixels with
// coverage >= 0.5, as drawn), measured once per atlas build; the ramp's
// characters sorted by it (ties keep the ramp's order), duplicates dropped.
void SceneStylizer::sortRamp()
{
	const int cw = atlas.getCellWidth(), ch = atlas.getCellHeight();
	if (sortedForCell != ch) {
		ofPixels px;
		atlas.getTexture().readToPixels(px);
		coverage.assign(AsciiFontAtlas::CHAR_COUNT, 0.0f);
		const int ncomp = (int)px.getNumChannels();
		for (int g = 0; g < AsciiFontAtlas::CHAR_COUNT; g++) {
			int lit = 0;
			for (int y = 0; y < ch && y < (int)px.getHeight(); y++)
				for (int x = 0; x < cw; x++) {
					const int xx = g * cw + x;
					if (xx < (int)px.getWidth() && px.getData()[((size_t)y * px.getWidth() + xx) * ncomp] >= 128)
						lit++;
				}
			coverage[g] = (float)lit / std::max(1, cw * ch);
		}
		sortedForCell = ch;
	}
	std::string uniq;
	for (char c : RAMP)
		if (c >= 32 && c <= 126 && uniq.find(c) == std::string::npos && (int)uniq.size() < MAX_RAMP)
			uniq.push_back(c);
	if (uniq.find(' ') == std::string::npos && !uniq.empty())
		uniq[0] = ' ';
	std::stable_sort(uniq.begin(), uniq.end(), [this](char a, char b) {
		return coverage[AsciiFontAtlas::indexOf(a)] < coverage[AsciiFontAtlas::indexOf(b)];
	});
	sortedRamp = uniq;
	sortedFrom = RAMP;
}

void SceneStylizer::render(ofTexture & warped, float blackPoint)
{
	if (!loaded || !atlas.isReady() || cols <= 0 || !outFbo.isAllocated())
		return;
	if (sortedFrom != RAMP)
		sortRamp();
	const float cw = (float)atlas.getCellWidth(), ch = (float)atlas.getCellHeight();
	ofPushStyle();
	ofEnableBlendMode(OF_BLENDMODE_DISABLED);
	ofSetColor(255);
	if (STYLE == STYLE_ASCII || STYLE == STYLE_BOTH) {
		cellFbo.begin();
		ofClear(0, 0, 0, 0);
		cellShader.begin();
		cellShader.setUniformTexture("warpedTex", warped, 0);
		cellShader.setUniform2f("cellSize", cw, ch);
		cellShader.setUniform2f("projSize", projRes.x, projRes.y);
		cellQuad.draw();
		cellShader.end();
		cellFbo.end();
		edgeFbo.begin();
		ofClear(0, 0, 0, 0);
		edgeShader.begin();
		edgeShader.setUniformTexture("warpedTex", warped, 0);
		edgeShader.setUniform2f("cellSize", cw, ch);
		edgeShader.setUniform2f("projSize", projRes.x, projRes.y);
		cellQuad.draw();
		edgeShader.end();
		edgeFbo.end();
	}
	float dirG[8];
	for (int i = 0; i < 8; i++)
		dirG[i] = (float)AsciiFontAtlas::indexOf(i < (int)DIR_GLYPHS.size() ? DIR_GLYPHS[i] : (i < 2 ? '-' : i < 4 ? '\\' : i < 6 ? '|' : '/'));
	// Debug colours per direction (no saturated red next to green).
	const float dbg[12] = { 0.0f, 0.78f, 1.0f,   1.0f, 0.82f, 0.0f,   1.0f, 0.25f, 1.0f,   1.0f, 1.0f, 1.0f };
	float ramp[MAX_RAMP];
	const int n = std::max(2, std::min(MAX_RAMP, (int)sortedRamp.size()));
	for (int i = 0; i < MAX_RAMP; i++)
		ramp[i] = (float)AsciiFontAtlas::indexOf(i < (int)sortedRamp.size() ? sortedRamp[i] : ' ');
	float pal[9];
	for (int i = 0; i < 3; i++) {
		pal[3 * i] = PALETTE[i].r; pal[3 * i + 1] = PALETTE[i].g; pal[3 * i + 2] = PALETTE[i].b;
	}
	outFbo.begin();
	ofClear(0, 0, 0, 255);
	stylizeShader.begin();
	stylizeShader.setUniformTexture("warpedTex", warped, 0);
	stylizeShader.setUniformTexture("cellTex", cellFbo.getTexture(), 1);
	stylizeShader.setUniformTexture("atlasTex", atlas.getTexture(), 2);
	stylizeShader.setUniformTexture("edgeTex", edgeFbo.getTexture(), 3);
	stylizeShader.setUniform1f("edgeThreshold", EDGE_THRESHOLD);
	stylizeShader.setUniform1fv("dirGlyph", dirG, 8);
	stylizeShader.setUniform1f("weightSplit", WEIGHT_SPLIT);
	stylizeShader.setUniform1i("debugEdges", DEBUG_EDGES ? 1 : 0);
	stylizeShader.setUniform3fv("debugColor", dbg, 4);
	stylizeShader.setUniform1i("lineScreen", LINE_SCREEN ? 1 : 0);
	stylizeShader.setUniform1f("linePeriod", (float)std::max(1, std::min(8, LINE_PERIOD)));
	stylizeShader.setUniform1i("style", std::max(0, std::min(3, STYLE)));
	stylizeShader.setUniform2f("cellSize", cw, ch);
	stylizeShader.setUniform1fv("rampGlyph", ramp, MAX_RAMP);
	stylizeShader.setUniform1f("rampCount", (float)n);
	stylizeShader.setUniform1f("luminanceGamma", std::max(0.05f, LUMINANCE_GAMMA));
	stylizeShader.setUniform1f("blackPoint", blackPoint);
	stylizeShader.setUniform1f("colourBoost", COLOUR_BOOST);
	stylizeShader.setUniform1f("dotSize", (float)std::max(1, std::min(8, DOT_SIZE)));
	stylizeShader.setUniform1f("levels", (float)std::max(2, std::min(16, LEVELS)));
	stylizeShader.setUniform1i("bayer8", BAYER8 ? 1 : 0);
	stylizeShader.setUniform1i("paletteOn", PALETTE_ON ? 1 : 0);
	stylizeShader.setUniform1f("paletteStops", (float)(PALETTE_STOPS <= 2 ? 2 : 3));
	stylizeShader.setUniform3fv("paletteColor", pal, 3);
	stylizeShader.setUniform1f("bothThreshold", BOTH_THRESHOLD);
	projQuad.draw();
	stylizeShader.end();
	outFbo.end();
	ofPopStyle();
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
}
