#include "SceneHeightInput.h"
#include "SceneGL.h"
#include "../../Games/VegetationField.h"

// Defaults (DEVLOG, 2026-11-18).
bool SceneHeightInput::MOCK_TERRAIN = false;
float SceneHeightInput::MOCK_WATER_LEVEL_MM = -12.0f;
bool SceneHeightInput::MOCK_FLAT = false;
bool SceneHeightInput::MOCK_HAND = false;
float SceneHeightInput::MOCK_HAND_SPEED = 2.0f;
float SceneHeightInput::MOCK_NOISE_MM = 0.0f;
float SceneHeightInput::SMOOTH_RADIUS_CELLS = 3.0f;
float SceneHeightInput::SMOOTH_TIME_SEC = 0.5f;
float SceneHeightInput::WATER_THRESHOLD_MM = 1.0f;

bool SceneHeightInput::setup()
{
	loaded = SceneGL::load(mockShader, "sceneMock") && SceneGL::load(emaShader, "sceneHeightEma")
		&& SceneGL::load(blurShader, "sceneBlur");
	return loaded;
}

void SceneHeightInput::allocate(int c, int r)
{
	cols = c;
	rows = r;
	rawPix.allocate(c, r, OF_PIXELS_GRAY);
	waterPix.allocate(c, r, OF_PIXELS_RGBA);
	rawTex.allocate(c, r, GL_R32F, true);
	rawTex.setTextureMinMagFilter(GL_NEAREST, GL_NEAREST);
	waterTex.allocate(c, r, GL_RGBA32F, true);
	waterTex.setTextureMinMagFilter(GL_LINEAR, GL_LINEAR); // smooth shoreline in the scene pass
	waterTex.setTextureWrap(GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
	lastFinite.assign((size_t)c * r, 0.0f);
	prevDepth.assign((size_t)c * r, 0.0f);
	SceneGL::allocate(mockRaw, c, r, GL_RGBA32F, GL_NEAREST);
	SceneGL::allocate(mockWater, c, r, GL_RGBA32F, GL_LINEAR);
	for (int i = 0; i < 2; i++) {
		SceneGL::allocate(ema[i], c, r, GL_RGBA32F, GL_NEAREST);
		SceneGL::allocate(display[i], c, r, GL_RGBA32F, GL_LINEAR); // bilinear in the scene pass
	}
	SceneGL::allocate(blurX, c, r, GL_RGBA32F, GL_NEAREST);
	gridQuad = SceneGL::quad((float)c, (float)r);
	resetPending = true;
}

void SceneHeightInput::update(const VegetationField & vf, float dt, float animTime, uint64_t frame)
{
	if (!loaded)
		return;
	const int c = vf.getCols(), r = vf.getRows();
	const cv::Mat & elev = vf.getElevationGrid();
	if (c <= 0 || r <= 0 || elev.rows != r || elev.cols != c || elev.type() != CV_32F)
		return;
	if (c != cols || r != rows)
		allocate(c, r);
	const uint64_t t0 = ofGetElapsedTimeMicros();

	ofPushStyle();
	ofEnableBlendMode(OF_BLENDMODE_DISABLED);
	ofSetColor(255);
	ofTexture * raw = &rawTex;
	useMockWater = MOCK_TERRAIN;
	if (MOCK_TERRAIN) {
		for (int part = 0; part < 2; part++) {
			ofFbo & target = part == 0 ? mockRaw : mockWater;
			target.begin();
			ofClear(0, 0, 0, 0);
			mockShader.begin();
			mockShader.setUniform1i("part", part);
			mockShader.setUniform2f("gridSize", (float)c, (float)r);
			mockShader.setUniform1f("mmPerCell", vf.getMmPerCell());
			mockShader.setUniform1f("time", animTime);
			mockShader.setUniform1f("dt", dt);
			mockShader.setUniform1f("waterLevel", MOCK_WATER_LEVEL_MM);
			mockShader.setUniform1f("noiseMM", std::max(0.0f, MOCK_NOISE_MM));
			mockShader.setUniform1f("frame", (float)(frame % 100000));
			mockShader.setUniform1i("handOn", MOCK_HAND ? 1 : 0);
			mockShader.setUniform1f("handSpeed", MOCK_HAND_SPEED);
			mockShader.setUniform1i("flatTerrain", MOCK_FLAT ? 1 : 0);
			gridQuad.draw();
			mockShader.end();
			target.end();
		}
		raw = &mockRaw.getTexture();
	} else {
		// Live: elevation (non-finite samples keep their last finite value)
		// and the simulated water, fresh, from read-only accessors.
		float lo = 0.0f, hi = 0.0f;
		vf.getElevationRange(lo, hi);
		const float rangeMM = std::fabs(hi - lo);
		float * h = rawPix.getData();
		float * w = waterPix.getData();
		const float invDt = dt > 0.0f ? 1.0f / dt : 0.0f;
		for (int gy = 0; gy < r; gy++) {
			const float * e = elev.ptr<float>(gy);
			for (int gx = 0; gx < c; gx++) {
				const size_t i = (size_t)gy * c + gx;
				if (std::isfinite(e[gx]))
					lastFinite[i] = e[gx];
				h[i] = lastFinite[i];
				const float d = std::max(0.0f, vf.getWaterDepthAt(gx, gy)) * rangeMM;
				const ofVec2f v = vf.getFlowVelocityAt(gx, gy);
				w[4 * i + 0] = d;
				w[4 * i + 1] = v.x;
				w[4 * i + 2] = v.y;
				w[4 * i + 3] = resetPending ? 0.0f : (d - prevDepth[i]) * invDt;
				prevDepth[i] = d;
			}
		}
		rawTex.loadData(rawPix);
		waterTex.loadData(waterPix);
	}

	// Display height: EMA, then Gaussian X, then Gaussian Y with the rate.
	const float alpha = SMOOTH_TIME_SEC > 0.0f ? 1.0f - std::exp(-std::max(0.0f, dt) / SMOOTH_TIME_SEC) : 1.0f;
	const int next = 1 - cur;
	ema[next].begin();
	ofClear(0, 0, 0, 0);
	emaShader.begin();
	emaShader.setUniformTexture("rawTex", *raw, 0);
	emaShader.setUniformTexture("prevTex", ema[cur].getTexture(), 1);
	emaShader.setUniform1f("alpha", alpha);
	emaShader.setUniform1i("resetAll", resetPending ? 1 : 0);
	gridQuad.draw();
	emaShader.end();
	ema[next].end();

	const float sigma = std::max(0.0f, SMOOTH_RADIUS_CELLS);
	blurX.begin();
	ofClear(0, 0, 0, 0);
	blurShader.begin();
	blurShader.setUniformTexture("srcTex", ema[next].getTexture(), 0);
	blurShader.setUniformTexture("prevTex", display[cur].getTexture(), 1);
	blurShader.setUniform2f("dir", 1.0f, 0.0f);
	blurShader.setUniform1f("sigma", sigma);
	blurShader.setUniform1i("rate", 0);
	blurShader.setUniform1f("dt", dt);
	blurShader.setUniform1i("resetAll", resetPending ? 1 : 0);
	gridQuad.draw();
	blurShader.end();
	blurX.end();

	display[next].begin();
	ofClear(0, 0, 0, 0);
	blurShader.begin();
	blurShader.setUniformTexture("srcTex", blurX.getTexture(), 0);
	blurShader.setUniformTexture("prevTex", display[cur].getTexture(), 1);
	blurShader.setUniform2f("dir", 0.0f, 1.0f);
	blurShader.setUniform1f("sigma", sigma);
	blurShader.setUniform1i("rate", 1);
	blurShader.setUniform1f("dt", dt);
	blurShader.setUniform1i("resetAll", resetPending ? 1 : 0);
	gridQuad.draw();
	blurShader.end();
	display[next].end();
	cur = next;
	resetPending = false;
	ofPopStyle();
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
	lastCpuMs = (ofGetElapsedTimeMicros() - t0) / 1000.0f;
}
