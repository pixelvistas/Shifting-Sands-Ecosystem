/***********************************************************************
RippleField - Prototype 4 ("Scene"): ripples on the simulated water, a
damped 2D wave equation on a ping-pong float texture at RIPPLE_MULT x the
grid resolution (rippleStep.frag; SUBSTEPS steps per frame).

Display only: the output perturbs the water's normals in the scene pass
and never feeds back into any simulation. Waves live only where the
simulated (or mock) water depth exceeds the water threshold; dry cells
are held at 0, i.e. the shore is a fixed boundary that reflects waves
(with inverted phase) and DAMPING_PER_SEC removes them. The wave height is clamped to MAX_AMPLITUDE,
so a source that never stops (a hand held in the water, Kinect flicker at
an object's edge) cannot grow the waves without bound. Damping and the
number of substeps follow the frame time, so ripples behave the same at
any frame rate (SUBSTEPS is the count at 60 fps).

Sources, per second (frame-rate independent): a hand entering or sand
falling in (|d(display height)/dt| above HEIGHT_RATE_THRESHOLD, gain
HEIGHT_GAIN, switch SOURCE_HEIGHT; |d(water depth)/dt| above
WATER_RATE_THRESHOLD, gain WATER_GAIN, switch SOURCE_WATER), capped at
IMPULSE_CAP per second, and autonomous
drops (DROP_RATE per second per 0.1 m^2 of grid area, amplitude
DROP_AMP), standing in for aquatic movement until the Lenia step. Drops
stop while "Freeze animation" is on.
***********************************************************************/

#pragma once
#include "ofMain.h"
#include <random>

class RippleField
{
public:
	bool setup();
	void reset() { resetPending = true; }
	// height/water: SceneHeightInput's textures (grid resolution).
	void update(ofTexture & height, ofTexture & water, int gridCols, int gridRows, float mmPerCell,
		float dt, bool dropsOn, uint64_t frame);
	ofTexture & getTexture() { return state[cur].getTexture(); }
	int getMult() const { return mult; }

	static int RIPPLE_MULT;            // ripple cells per grid cell (1..4)
	static int SUBSTEPS;               // at 60 fps; scaled with the frame time
	static float WAVE_C2;              // (c dt / dx)^2 per substep, <= 0.5
	static float DAMPING_PER_SEC;      // amplitude e-folding rate
	static float MAX_AMPLITUDE;
	static bool SOURCE_HEIGHT;
	static bool SOURCE_WATER;
	static float HEIGHT_RATE_THRESHOLD; // mm/s
	static float WATER_RATE_THRESHOLD;  // mm/s
	static float HEIGHT_GAIN;          // impulse per second per mm/s of excess
	static float WATER_GAIN;
	static float IMPULSE_CAP;          // impulse per second
	static float DROP_RATE;            // drops per second per 0.1 m^2
	static float DROP_AMP;
	static float DROP_RADIUS_MM;       // Gaussian sigma; rings ~4 sigma apart

private:
	ofShader stepShader;
	bool loaded = false;
	ofFbo state[2];
	int cur = 0, mult = 0, cols = 0, rows = 0;
	bool resetPending = true;
	ofMesh quad;
	std::mt19937 rng{ 20261118u };     // drops: reproducible
};
