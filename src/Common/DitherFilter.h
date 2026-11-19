/***********************************************************************
DitherFilter - a screen-space ordered-dither filter: takes a texture
(any rectangle texture, e.g. a whole display mode's output captured in an
FBO) and draws it as dots. Knows nothing about the mode that produced the
picture, so any display mode can put its output through it.

Two passes (shaders in bin/data/shaders/dither/GL2|GL3):
1. ditherDots: per dot (dotSize x dotSize output pixels, grid from the
   top-left), the mean of the pre-filtered pixels (exposure, contrast and
   saturation; hue kept).
2. ditherOut: per pixel, the dot's mean quantised against a threshold
   fixed in output pixels (Bayer 4 x 4 / 8 x 8, horizontal line screen,
   or a rotated clustered-dot halftone; nothing depends on time):
   - Hue (default): luminance quantised to `levels`, drawn in the dot's
     own hue at that luminance (colours stay, as dots);
   - Per channel: each channel quantised;
   - Palette: luminance through 2 or 3 stops (Gold, Night, Green).
   Dots at or below the black point are black. Line protection draws
   thin lines (pixels brighter, or darker, than the image on both sides
   of them along some direction) undithered on top.

Capture helper: begin(w, h) ... end() draws into the filter's own FBO,
cleared to opaque black and multisampled like the target the filter is
used on, then drawCaptured() draws it filtered. All GL objects are made
lazily on first use, so use one instance from one GL context (one
window): FBOs and timer queries are not shared between contexts.
Not compiled with openFrameworks here, not hardware-tested.
***********************************************************************/

#pragma once
#include "ofMain.h"

class DitherFilter
{
public:
	enum { MODE_HUE = 0, MODE_CHANNEL = 1, MODE_PALETTE = 2 };
	enum { PATTERN_BAYER4 = 0, PATTERN_BAYER8 = 1, PATTERN_LINES = 2, PATTERN_HALFTONE = 3 };
	enum { PROTECT_OFF = 0, PROTECT_BRIGHT = 1, PROTECT_BRIGHT_DARK = 2 };
	enum { PALETTE_GOLD = 0, PALETTE_NIGHT = 1, PALETTE_GREEN = 2 };

	struct Params {
		bool enabled;
		int mode;
		int pattern;
		int dotSize;            // output pixels, 1..8
		int levels;             // 2..16
		float blackPoint;       // luminance
		float exposure;         // x colour
		float contrast;         // on luminance, about 0.5
		float saturation;       // 0 grey, 1 unchanged
		int lineProtect;        // PROTECT_*
		float lineThreshold;    // luminance step on both sides
		int linePeriod;         // line screen, pixels
		float halftoneAngle;    // degrees
		int halftoneCell;       // dots per halftone cell side
		int palettePreset;
		int paletteStops;       // 2 or 3
		ofFloatColor palette[3];
	};
	static const Params DEFAULTS;   // defined at the top of DitherFilter.cpp
	Params params = DEFAULTS;

	static void applyPalettePreset(Params & p, int preset);

	// Capture: returns false (and captures nothing) if the shaders are not
	// loaded, so the caller can draw directly instead.
	bool begin(int w, int h);
	void end();
	void drawCaptured(float x, float y);
	// src: a GL_TEXTURE_RECTANGLE texture; anything else is drawn as it is.
	void draw(const ofTexture & src, float x, float y);
	bool isReady();

	// Widgets only, under a collapsing header, into the current ImGui window.
	void drawGui();
	void setPresetFile(const std::string & path) { presetFile = path; }
	bool savePreset();
	bool loadPreset();      // reads only the entries present

	float gpuMs() const { return timer.ms(); }

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
	void ensureDots(int w, int h, int ds);

	ofShader dotsShader, outShader;
	int loadState = 0;      // 0 not tried, 1 loaded, -1 failed
	ofFbo captureFbo, dotFbo;
	int captureW = 0, captureH = 0, captureSamples = -1;
	int dotW = 0, dotH = 0, dotDs = 0, srcW = 0, srcH = 0;
	ofMesh dotQuad, srcQuad;
	bool capturing = false;
	GpuTimer timer;
	std::string presetFile = "settings/ditherFilter.xml";
	std::string presetStatus;
};
