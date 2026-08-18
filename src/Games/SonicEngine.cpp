#include "SonicEngine.h"

float SonicEngine::MIN_FREQ = 120.0f;
float SonicEngine::MAX_FREQ = 880.0f;
float SonicEngine::MASTER_GAIN = 0.3f;

void SonicEngine::setup()
{
	ofSoundStreamSettings settings;
	settings.setOutListener(this);
	settings.sampleRate = sampleRate;
	settings.numOutputChannels = 2;
	settings.numInputChannels = 0;
	settings.bufferSize = 256;
	settings.numBuffers = 4;
	soundStream.setup(settings);
}

void SonicEngine::exit()
{
	soundStream.close();
}

int SonicEngine::allocateVoice()
{
	for (int i = 0; i < NUM_VOICES; i++) {
		bool expected = false;
		if (voices[i].active.compare_exchange_strong(expected, true)) {
			voices[i].targetGain = 0.0f; // silent until the caller sets real values
			return i;
		}
	}
	return -1; // pool full - caller just goes unsonified
}

void SonicEngine::setVoice(int idx, float freq, float gain01, float panX, float panY)
{
	if (idx < 0 || idx >= NUM_VOICES) return;
	voices[idx].targetFreq = freq;
	voices[idx].targetGain = gain01;
	voices[idx].targetPanX = panX;
	voices[idx].targetPanY = panY;
}

void SonicEngine::releaseVoice(int idx)
{
	if (idx < 0 || idx >= NUM_VOICES) return;
	voices[idx].targetGain = 0.0f;
	// Frees itself (active -> false) in audioOut() once actually silent.
}

void SonicEngine::audioOut(ofSoundBuffer & buffer)
{
	buffer.set(0.0f);

	size_t numFrames = buffer.getNumFrames();
	size_t numChannels = buffer.getNumChannels();
	std::vector<float> & data = buffer.getBuffer();

	const float smoothing = 0.002f; // one-pole coefficient per sample, ~11ms at 44.1kHz

	for (auto & v : voices) {
		if (!v.active.load(std::memory_order_relaxed))
			continue;

		float targetFreq = v.targetFreq.load(std::memory_order_relaxed);
		float targetGain = v.targetGain.load(std::memory_order_relaxed);
		float targetPanX = v.targetPanX.load(std::memory_order_relaxed);

		for (size_t frame = 0; frame < numFrames; frame++) {
			v.smoothedFreq += (targetFreq - v.smoothedFreq) * smoothing;
			v.smoothedGain += (targetGain - v.smoothedGain) * smoothing;
			v.smoothedPanX += (targetPanX - v.smoothedPanX) * smoothing;

			v.phase += v.smoothedFreq / sampleRate;
			if (v.phase >= 1.0) v.phase -= 1.0;

			float sample = (float)sin(v.phase * TWO_PI) * v.smoothedGain * MASTER_GAIN;

			// Constant-power stereo pan. panY rides along unused until a
			// quad mix replaces this block - see the header note.
			float leftGain = sqrt(0.5f * (1.0f - v.smoothedPanX));
			float rightGain = sqrt(0.5f * (1.0f + v.smoothedPanX));

			if (numChannels >= 2) {
				data[frame * numChannels + 0] += sample * leftGain;
				data[frame * numChannels + 1] += sample * rightGain;
			} else if (numChannels == 1) {
				data[frame * numChannels + 0] += sample;
			}
		}

		// Released and now inaudible - free the slot for reuse.
		if (targetGain <= 0.0001f && v.smoothedGain < 0.0005f) {
			v.active.store(false, std::memory_order_relaxed);
		}
	}

	for (size_t i = 0; i < data.size(); i++) {
		data[i] = (float)tanh(data[i]); // soft-clip the mix
	}
}
