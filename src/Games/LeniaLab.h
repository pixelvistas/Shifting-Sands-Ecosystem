/***********************************************************************
LeniaLab - "Lenia lab" display mode (branch prototype/6-lenia): Lenia
organisms on their own, on black. Reads and draws no terrain, water,
succession or plants; the rest of the app is paused while it is shown
(ofApp). A test bed for how organisms move and behave on the sand; the
mouse in the main window stands in for a hand.

Simulation on the GPU (shaders bin/data/shaders/lenia/GL3|GL2), RGBA32F
ping-pong buffers, up to three channels (rgb), one texel per world cell:
- Lenia (Chan 2019): U = K * A, A += dt G(U), clipped to [0, 1]; kernels of
  concentric shells (b, R), growth polynomial / Gaussian / step;
  multi-kernel and multi-channel as Chan's LeniaNDKC.py (sum of h G over
  the kernels into a channel, divided by the sum of h).
- Flow-Lenia (Plantec et al. 2023): affinity U = sum h G, flow
  F = grad U (1 - alpha) - grad(sum A) alpha, matter moved by reintegration
  tracking (mass conserved); optional parameter embedding: species share
  channel 0 and a parameter map P (slot weights) travels with the matter
  (mixed by mass-weighted average or stochastic pick at collisions).
Kernels are convolved directly from a tap table (LeniaSpecies), so each
step costs taps x cells; R is limited to ~36 cells (R scale).

World in mm: the world covers the projector image; cells = projector px /
(px per mm x cell mm), so a creature's size and speed on the sand follow
from cell mm, R and the time scale (simulated time units per second).

Interaction (main window, over the preview): spawn a preset at the cursor
(rotation), add / erase mass with a brush, paint the mask or gradient.
Mask (none / circle / painted): growth zero outside (Lenia: "outside
growth" sets it; Flow-Lenia: affinity zero outside, optional edge
repulsion). Gradient (linear / painted): growth (affinity) bias or
advection (drift).

All GPU work runs in update() (main window's context); both windows draw
the result texture (shared). The dither filter (own instance, off by
default) is applied in the projector window by ofApp.

Species and presets: bin/data/lenia/species.json (sources beside each);
GUI state: bin/data/settings/leniaPreset.xml.
Not compiled with openFrameworks here, not hardware-tested.
***********************************************************************/

#pragma once
#include "ofMain.h"
#include "LeniaSpecies.h"
#include "../Common/DitherFilter.h"
#include <deque>

class LeniaLab
{
public:
	enum Tool { TOOL_SPAWN = 0, TOOL_ADD, TOOL_ERASE, TOOL_MASK_PAINT, TOOL_MASK_ERASE, TOOL_GRAD_PAINT, TOOL_GRAD_ERASE };
	enum { MASK_NONE = 0, MASK_CIRCLE = 1, MASK_PAINTED = 2 };
	enum { GRAD_OFF = 0, GRAD_LINEAR = 1, GRAD_PAINTED = 2 };

	struct Params {
		// space and time
		float pxPerMm;            // projector pixels per mm on the sand
		float cellMm;             // world cell size
		float rScale;             // kernel and pattern scale (R x rScale cells)
		float timeScale;          // simulated time units per second
		float dt;                 // step (Lenia 1/T; Flow-Lenia dt); set from the species
		bool wrapWorld;           // torus instead of walls
		int maxStepsPerFrame;
		bool paused;
		// tools
		int tool;
		int spawnChannel;         // channel (or slot) the spawned preset goes to
		float spawnDeg;
		bool spawnRandomRot;
		float brushMm;
		float brushAmount;
		int scatterCount;
		float noiseMm;
		// mask
		int maskMode;
		float maskRadiusMm;
		float maskEdgeMm;
		float maskCentreX, maskCentreY;   // 0..1 of the world
		float outsideGrowth;      // Lenia: growth rate outside (0 = none, -1 dissolves)
		float edgeRepel;          // Flow-Lenia: inward flow at the edge, cells / unit time
		float maskShow;
		// gradient
		int gradMode;
		float gradDeg;
		float gradStrength;
		int gradTarget;           // 0 growth (affinity) bias, 1 drift (advection / flow)
		float gradShow;
		// embedding
		int mixRule;              // 0 average, 1 stochastic
		// drawing
		ofFloatColor colour[3];
		float gain, gammaExp;
		float glow, glowMm;
		bool trailOn;
		float trailSec;
		bool scaleBar;
	};
	static const Params DEFAULTS;   // defined at the top of LeniaLab.cpp
	Params params = DEFAULTS;

	void setup(int projW, int projH);
	void setProjectorSize(int w, int h);
	// Advances the simulation by frameSec and renders the output texture.
	void update(double frameSec);
	// Draw the rendered output (projector window: at its size; main window: preview rect).
	void drawOutput(float x, float y, float w, float h);
	void drawGui();
	// Main-window mouse, in window coordinates; the preview rectangle maps to the world.
	void setPreviewRect(const ofRectangle & r) { previewRect = r; }
	ofRectangle previewRectFor(const ofRectangle & area) const;
	void mousePressed(float x, float y, int button);
	void mouseDragged(float x, float y, int button);
	void mouseReleased(float x, float y, int button);
	void keyPressed(int key);

	DitherFilter dither;     // applied by ofApp in the projector window

	bool savePreset();
	bool loadPreset();

private:
	struct GpuTimer {
		void begin();
		void end();
		float ms() const { return lastMs; }
	private:
		unsigned int ids[2] = { 0, 0 };
		bool pending[2] = { false, false };
		bool started = false;
		int cur = 0;
		bool supported = true, checked = false;
		float lastMs = 0.0f;
	};

	bool loadShaders();
	bool ensureWorld();                 // (re)allocates the world buffers for the current sizes
	void resetWorld();
	bool applyWorldConfig(bool keepDt);   // assembles kernels from the channel species
	bool rebuildKernels();               // after kernel edits
	void uploadTaps();
	void step();
	void conv(ofFbo & out, int mode);
	void setEnvUniforms(ofShader & s);
	void edit(int op, const ofVec2f & centre, const lenia::Grid * stampGrids, int nStamp, int stampBase, int slot);
	void stampSpecies(int channel, const ofVec2f & cell, float deg);
	void scatter();
	void noisePatches();
	void reduceAndCount();
	void render(float frac);
	ofVec2f windowToCell(float x, float y) const;
	void applyTool(const ofVec2f & cell, bool first);
	void drawPass(ofFbo & target, ofShader & s, float tw, float th);
	int worldW() const { return worldWc; }
	int worldH() const { return worldHc; }
	float mmToCells(float mm) const { return mm / std::max(0.05f, params.cellMm); }
	const lenia::Species * channelSpecies(int c) const;
	void drawKernelGui();
	void setMode(int model);

	// library and world
	lenia::Library lib;
	std::string libStatus;
	lenia::WorldConfig cfg;
	lenia::WorldSetup setupW;
	std::string worldStatus;
	int worldChoice = -1;

	// GL
	ofShader convShader, flowShader, moveShader, editShader, reduceShader, renderShader, trailShader;
	int loadState = 0;
	ofFbo state[2], param[2], aff, disp[2], envBuf[2], reduceFbo, outFbo, trailFbo[2];
	ofTexture tapTex, stampTex;
	int cur = 0, envCur = 0, trailCur = 0;
	int worldWc = 0, worldHc = 0, projW = 1280, projH = 800;
	float allocCellMm = 0, allocPxPerMm = 0;
	bool allocWrap = false;
	ofMesh quad;
	double acc = 0.0;
	bool prevValid = false;
	long long steps = 0;
	double simTime = 0.0;
	GpuTimer timer;

	// readouts
	int frameCount = 0;
	float mass[3] = { 0, 0, 0 };
	float massAtReset[3] = { 0, 0, 0 };
	bool massResetPending = true;
	std::deque<float> massHistory[3];
	int creatures = 0;
	float stepsPerSec = 0;

	// mouse
	ofRectangle previewRect;
	bool dragging = false;
	ofVec2f lastCell;

	std::string presetFile = "settings/leniaPreset.xml";
	std::string presetStatus;
	ofRectangle outRectCache;
};
