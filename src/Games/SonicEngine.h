/***********************************************************************
SonicEngine.h - a small multi-voice sine synth driving the sandbox's
audio output directly through core openFrameworks (ofSoundStream), with
no third-party audio addon. Given this project's history of needing
hand-written compatibility patches for every addon on oF 0.12.1
(see PATCHES.md), avoiding a new one here is deliberate.

One Voice per sonified SonicParticle: frequency (pitch, from speed),
gain (volume, from height), and stereo pan (from x position) are set
continuously from the main thread; the audio thread smooths toward
those targets and mixes. A voice fades to silence and frees itself
automatically once released rather than cutting off hard, so a
particle's death is inaudible as a click.

Pan is tracked as (panX, panY) even though only panX drives the current
stereo (2-channel) mix - panY is inert today so that moving to quad
later is a channel-count and mixing-function change here, not a change
to how callers describe a voice's position.

Ecosystem extension, part of the Shifting Sands fork of Magic Sand.
***********************************************************************/

#pragma once
#include "ofMain.h"
#include <array>
#include <atomic>

class SonicEngine : public ofBaseSoundOutput {
public:
	void setup();
	void exit();

	void audioOut(ofSoundBuffer & buffer) override;

	// Returns a voice index, or -1 if the pool is full (caller just goes
	// unsonified - the physics keeps running regardless of audio).
	int allocateVoice();
	// freq in Hz, gain01 in [0,1], panX/panY in [-1,1].
	void setVoice(int idx, float freq, float gain01, float panX, float panY);
	// Fades the voice to silence; it frees itself once inaudible.
	void releaseVoice(int idx);

	static const int NUM_VOICES = 32;

	// Tunable in the debug GUI.
	static float MIN_FREQ, MAX_FREQ; // Hz, the speed-to-pitch mapping range
	static float MASTER_GAIN;

private:
	struct Voice {
		std::atomic<bool> active{false};
		std::atomic<float> targetFreq{220.0f};
		std::atomic<float> targetGain{0.0f};
		std::atomic<float> targetPanX{0.0f};
		std::atomic<float> targetPanY{0.0f};

		// Audio-thread-only state.
		double phase = 0.0;
		float smoothedFreq = 220.0f;
		float smoothedGain = 0.0f;
		float smoothedPanX = 0.0f;
	};

	ofSoundStream soundStream;
	std::array<Voice, NUM_VOICES> voices;
	double sampleRate = 44100.0;
};
