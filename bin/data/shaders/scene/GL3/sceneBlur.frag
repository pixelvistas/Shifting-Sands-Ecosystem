// Prototype 4 (Scene), display height step 2: separable Gaussian (sigma in
// grid cells, taps to 3 sigma, at most 12 each side), one direction per
// draw. With rate = 1 (the second draw) it also writes the rate of change
// against the previous display height: G = d(height)/dt (mm/s), used only
// as a ripple source. Output R = height (mm), A = 1. Keep in sync with
// ../GL2/sceneBlur.frag.
#version 150

uniform sampler2DRect srcTex;   // R height (mm)
uniform sampler2DRect prevTex;  // previous display height (rate = 1 only)
uniform vec2 dir;               // (1, 0) or (0, 1)
uniform float sigma;            // cells (0 = no blur)
uniform int rate;
uniform float dt;
uniform int resetAll;

in vec2 coord;
out vec4 outputColor;

void main()
{
	float h = texture(srcTex, coord).r;
	if (sigma > 0.05) {
		float r = min(12.0, ceil(3.0 * sigma));
		float wsum = 1.0;
		for (int k = 1; k <= 12; k++) {
			float fk = float(k);
			if (fk > r)
				break;
			float w = exp(-0.5 * fk * fk / (sigma * sigma));
			h += w * (texture(srcTex, coord + dir * fk).r + texture(srcTex, coord - dir * fk).r);
			wsum += 2.0 * w;
		}
		h /= wsum;
	}
	float dh = 0.0;
	if (rate == 1) {
		vec4 p = texture(prevTex, coord);
		dh = (resetAll == 1 || p.a < 0.5 || dt <= 0.0) ? 0.0 : (h - p.r) / dt;
	}
	outputColor = vec4(h, dh, 0.0, 1.0);
}
