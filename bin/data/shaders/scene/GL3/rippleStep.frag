// Prototype 4 (Scene), RippleField (ping-pong, ripple resolution =
// rippleMult x grid): a damped 2D wave equation, display only (it feeds
// water normals and nothing else). State: R = u (this step), G = u (the
// step before). Only on water (depth > waterThreshold, fresh every frame);
// dry cells hold u = 0, so the shore is a fixed (reflecting) boundary.
//   u' = u + (u - u_prev)(1 - damping) + c2 lap(u)   (c2 <= 0.5: stable)
// Sources (first substep only, impulseScale = 1; else 0): a depression of
// min(cap, gain x excess) where |d(display height)/dt| exceeds
// heightRateThreshold or |d(water depth)/dt| exceeds waterRateThreshold
// (a hand entering, sand falling in), and autonomous drops: up to 8 per
// frame, drawn on the CPU (Poisson, seeded), each a Gaussian depression
// of dropAmp, radius 1.5 ripple cells, applied only where there is water.
// Keep in sync with ../GL2/rippleStep.frag; tools/scene_preview runs this file.
#version 150

uniform sampler2DRect prevTex;
uniform sampler2DRect heightTex;   // grid: R height, G d(height)/dt
uniform sampler2DRect waterTex;    // grid: R depth (mm), A d(depth)/dt
uniform float rippleMult;          // ripple cells per grid cell
uniform float waterThreshold;      // mm
uniform float c2;
uniform float damping;
uniform float heightRateThreshold; // mm/s
uniform float waterRateThreshold;  // mm/s
uniform float impulseGain;
uniform float impulseCap;
uniform float impulseScale;        // 1 on the first substep, else 0
uniform vec2 drops[8];             // ripple cell coordinates
uniform float dropCount;
uniform float dropAmp;
uniform int resetAll;

in vec2 coord;                     // ripple texel coordinate
out vec4 outputColor;

float wetAt(vec2 c)
{
	return (texture(waterTex, c / rippleMult).r > waterThreshold) ? 1.0 : 0.0;
}

void main()
{
	vec4 w = texture(waterTex, coord / rippleMult);
	if (resetAll == 1 || w.r <= waterThreshold) {
		outputColor = vec4(0.0);
		return;
	}
	vec4 s = texture(prevTex, coord);
	float u = s.r;
	float up = s.g;
	float n = wetAt(coord + vec2(1.0, 0.0)) * texture(prevTex, coord + vec2(1.0, 0.0)).r
		+ wetAt(coord - vec2(1.0, 0.0)) * texture(prevTex, coord - vec2(1.0, 0.0)).r
		+ wetAt(coord + vec2(0.0, 1.0)) * texture(prevTex, coord + vec2(0.0, 1.0)).r
		+ wetAt(coord - vec2(0.0, 1.0)) * texture(prevTex, coord - vec2(0.0, 1.0)).r;
	float un = u + (u - up) * (1.0 - damping) + c2 * (n - 4.0 * u);
	if (impulseScale > 0.0) {
		float hr = abs(texture(heightTex, coord / rippleMult).g);
		float excess = max(hr - heightRateThreshold, 0.0) + max(abs(w.a) - waterRateThreshold, 0.0);
		un -= min(impulseCap, impulseGain * excess) * impulseScale;
		for (int k = 0; k < 8; k++) {
			if (float(k) >= dropCount)
				break;
			vec2 dd = coord - drops[k];
			float r2 = dot(dd, dd);
			if (r2 < 25.0)
				un -= dropAmp * exp(-r2 / 4.5);
		}
	}
	outputColor = vec4(un, u, 0.0, 1.0);
}
