#include "RippleField.h"
#include "SceneGL.h"
#include "SceneHeightInput.h"
#include <random>

// Defaults (DEVLOG, 2026-11-18; chosen on the headless preview).
int RippleField::RIPPLE_MULT = 2;
int RippleField::SUBSTEPS = 2;
float RippleField::WAVE_C2 = 0.35f;
float RippleField::DAMPING = 0.012f;
float RippleField::HEIGHT_RATE_THRESHOLD = 15.0f;
float RippleField::WATER_RATE_THRESHOLD = 8.0f;
float RippleField::IMPULSE_GAIN = 0.01f;
float RippleField::IMPULSE_CAP = 1.0f;
float RippleField::DROP_RATE = 0.6f;
float RippleField::DROP_AMP = 0.5f;

bool RippleField::setup()
{
	loaded = SceneGL::load(stepShader, "rippleStep");
	return loaded;
}

void RippleField::update(ofTexture & height, ofTexture & water, int gridCols, int gridRows, float mmPerCell,
	float dt, bool dropsOn, uint64_t frame)
{
	if (!loaded || gridCols <= 0 || gridRows <= 0)
		return;
	const int m = std::max(1, std::min(4, RIPPLE_MULT));
	if (m != mult || gridCols != cols || gridRows != rows) {
		mult = m;
		cols = gridCols;
		rows = gridRows;
		for (int i = 0; i < 2; i++)
			SceneGL::allocate(state[i], cols * mult, rows * mult, GL_RGBA32F, GL_LINEAR);
		quad = SceneGL::quad((float)(cols * mult), (float)(rows * mult));
		resetPending = true;
	}
	// Autonomous drops this frame: Poisson with mean DROP_RATE x dt x (grid
	// area / 0.1 m^2), at most 8, uniform over the grid (the shader keeps
	// only those that land on water).
	float drops[16];
	int dropCount = 0;
	if (dropsOn && DROP_RATE > 0.0f && dt > 0.0f) {
		const double areaMM2 = (double)gridCols * gridRows * mmPerCell * mmPerCell;
		std::poisson_distribution<int> count(DROP_RATE * dt * areaMM2 / 1.0e5);
		std::uniform_real_distribution<float> ux(0.0f, (float)(gridCols * mult)), uy(0.0f, (float)(gridRows * mult));
		dropCount = std::min(8, count(rng));
		for (int k = 0; k < dropCount; k++) {
			drops[2 * k] = ux(rng);
			drops[2 * k + 1] = uy(rng);
		}
	}
	for (int k = dropCount; k < 8; k++)
		drops[2 * k] = drops[2 * k + 1] = -100.0f;
	ofPushStyle();
	ofEnableBlendMode(OF_BLENDMODE_DISABLED);
	ofSetColor(255);
	const int steps = std::max(1, std::min(8, SUBSTEPS));
	for (int s = 0; s < steps; s++) {
		const int next = 1 - cur;
		state[next].begin();
		ofClear(0, 0, 0, 0);
		stepShader.begin();
		stepShader.setUniformTexture("prevTex", state[cur].getTexture(), 0);
		stepShader.setUniformTexture("heightTex", height, 1);
		stepShader.setUniformTexture("waterTex", water, 2);
		stepShader.setUniform1f("rippleMult", (float)mult);
		stepShader.setUniform1f("waterThreshold", SceneHeightInput::WATER_THRESHOLD_MM);
		stepShader.setUniform1f("c2", ofClamp(WAVE_C2, 0.0f, 0.5f));
		stepShader.setUniform1f("damping", ofClamp(DAMPING, 0.0f, 1.0f));
		stepShader.setUniform1f("heightRateThreshold", HEIGHT_RATE_THRESHOLD);
		stepShader.setUniform1f("waterRateThreshold", WATER_RATE_THRESHOLD);
		stepShader.setUniform1f("impulseGain", IMPULSE_GAIN);
		stepShader.setUniform1f("impulseCap", IMPULSE_CAP);
		stepShader.setUniform1f("impulseScale", s == 0 ? 1.0f : 0.0f);
		stepShader.setUniform2fv("drops", drops, 8);
		stepShader.setUniform1f("dropCount", s == 0 ? (float)dropCount : 0.0f);
		stepShader.setUniform1f("dropAmp", DROP_AMP);
		stepShader.setUniform1i("resetAll", resetPending ? 1 : 0);
		quad.draw();
		stepShader.end();
		state[next].end();
		cur = next;
		resetPending = false;
	}
	ofPopStyle();
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
}
