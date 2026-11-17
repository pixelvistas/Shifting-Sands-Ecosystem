// Prototype 4 (Scene), RippleField (ping-pong, ripple resolution =
// rippleMult x grid): a damped 2D wave equation, display only (it feeds
// water normals and nothing else). State: R = u (this step), G = u (the
// step before). Only on water (depth > waterThreshold, fresh every frame);
// dry cells hold u = 0, so the shore is a fixed (reflecting) boundary.
//   u' = clamp(u + (u - u_prev)(1 - damping) + c2 lap(u), -maxAmp, maxAmp)
//   (c2 <= 0.5: stable; damping per substep, from a per-second rate on the
//   CPU; maxAmp bounds the waves whatever keeps feeding them)
// Sources (first substep only, impulseScale = the frame time in s; else
// 0): a depression of min(cap, gain x excess) per second where
// |d(display height)/dt| exceeds heightRateThreshold or |d(water depth)/dt|
// exceeds waterRateThreshold (a hand entering, sand falling in; each
// source can be off: gain 0), and autonomous drops: up to 8 per
// frame, drawn on the CPU (Poisson, seeded), each a Gaussian depression
// of dropAmp, sigma dropRadius ripple cells (so rings are several glyph
// cells wide), applied only where there is water. Impulses are added to
// both stored steps (u and u_prev): a displacement at rest, which then
// spreads as a ring, not a velocity kick (a kick keeps deepening the
// depression every substep).
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
uniform float heightGain;          // per second per mm/s of excess (0: off)
uniform float waterGain;           // per second per mm/s of excess (0: off)
uniform float impulseCap;          // per second
uniform float impulseScale;        // frame time (s) on the first substep, else 0
uniform float maxAmp;
uniform vec2 drops[8];             // ripple cell coordinates
uniform float dropCount;
uniform float dropAmp;
uniform float dropRadius;          // ripple cells (sigma)
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
	float kick = 0.0;
	if (impulseScale > 0.0) {
		float hr = abs(texture(heightTex, coord / rippleMult).g);
		float rate = heightGain * max(hr - heightRateThreshold, 0.0) + waterGain * max(abs(w.a) - waterRateThreshold, 0.0);
		kick -= min(impulseCap, rate) * impulseScale;
		for (int k = 0; k < 8; k++) {
			if (float(k) >= dropCount)
				break;
			vec2 dd = coord - drops[k];
			float r2 = dot(dd, dd);
			float s2 = max(0.25, dropRadius * dropRadius);
			if (r2 < 9.0 * s2)
				kick -= dropAmp * exp(-0.5 * r2 / s2);
		}
	}
	outputColor = vec4(clamp(un + kick, -maxAmp, maxAmp), clamp(u + kick, -maxAmp, maxAmp), 0.0, 1.0);
}
