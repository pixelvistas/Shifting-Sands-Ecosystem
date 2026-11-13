// Prototype 2 (Mycorrhiza), style "Patches and hyphae": the lit ground.
// Inside the presence mask, a curl-advected, two-level domain-warped fbm
// (swirling, fluid); colour by maturity (cyan, blue-white, amber, orange),
// cooler at the edges (presence gradient) with a thin brighter rim at
// presence 0.5; brightness proportional to presence. Drawn over black.
// Shares no code with heightMapShader. Keep in sync with ../GL3/patches.frag
// and with tools/mycorrhiza_harness/main.cpp (drawPatches, swirl, ramp4).
#version 120
#extension GL_ARB_texture_rectangle : enable

uniform sampler2DRect patchTex; // R presence, G maturity (MycorrhizaField), per grid cell
uniform float time;         // field time, s
uniform float cellMM;       // mm per grid cell
uniform float scaleMM;      // texture scale
uniform float flowSpeed;
uniform float contrast;
uniform float brightness;
uniform float edgeCool;     // edges shift toward the fresh (cool) colour
uniform float rimGain;      // thin brighter rim
uniform vec4 stopPos;       // ramp positions of the four stops
uniform vec3 stop0;         // cyan #3FD8FF
uniform vec3 stop1;         // blue-white #CFEFFF (also the rim)
uniform vec3 stop2;         // amber #FFB347
uniform vec3 stop3;         // orange #F07A2A

varying vec2 cellCoord;

// hash12 (Dave Hoskins), value noise, 4-octave fbm; the harness has the same in C++.
float hash12(vec2 p)
{
	vec3 p3 = fract(vec3(p.xyx) * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}

float vnoise(vec2 p)
{
	vec2 i = floor(p);
	vec2 f = p - i;
	vec2 u = f * f * (3.0 - 2.0 * f);
	float a = hash12(i), b = hash12(i + vec2(1.0, 0.0));
	float c = hash12(i + vec2(0.0, 1.0)), d = hash12(i + vec2(1.0, 1.0));
	float ab = mix(a, b, u.x), cd = mix(c, d, u.x);
	return mix(ab, cd, u.y);
}

float fbm(vec2 p)
{
	float v = 0.0, amp = 0.5;
	for (int o = 0; o < 4; o++) {
		v += amp * vnoise(p);
		p = p * 2.02 + vec2(1.7, 9.2);
		amp *= 0.5;
	}
	return v;
}

// Swirl 0..1 at grid cell coordinate c: curl-advected, two-level domain warp.
float swirl(vec2 c)
{
	float t = time * flowSpeed;
	vec2 b = c * cellMM / scaleMM;
	float e = 0.1;
	vec2 cp = b * 0.5 + vec2(3.1 + 0.05 * t, 7.7);
	vec2 curl = vec2(vnoise(cp + vec2(0.0, e)) - vnoise(cp - vec2(0.0, e)),
		-(vnoise(cp + vec2(e, 0.0)) - vnoise(cp - vec2(e, 0.0)))) / (2.0 * e);
	vec2 p = b + 0.5 * curl;
	vec2 q = vec2(fbm(p + vec2(0.10 * t, 0.0)), fbm(p + vec2(5.2, 1.3 - 0.08 * t)));
	vec2 r = vec2(fbm(p + 4.0 * q + vec2(1.7 + 0.12 * t, 9.2)), fbm(p + 4.0 * q + vec2(8.3, 2.8 - 0.10 * t)));
	float v = fbm(p + 4.0 * r);
	return clamp(0.5 + contrast * (v - 0.5), 0.0, 1.0);
}

// Maturity colour: cyan, blue-white, amber, orange at stopPos (increasing).
vec3 ramp4(float m)
{
	m = clamp(m, 0.0, 1.0);
	vec3 c01 = mix(stop0, stop1, clamp((m - stopPos.x) / max(stopPos.y - stopPos.x, 1e-4), 0.0, 1.0));
	vec3 c12 = mix(stop1, stop2, clamp((m - stopPos.y) / max(stopPos.z - stopPos.y, 1e-4), 0.0, 1.0));
	vec3 c23 = mix(stop2, stop3, clamp((m - stopPos.z) / max(stopPos.w - stopPos.z, 1e-4), 0.0, 1.0));
	return m <= stopPos.y ? c01 : (m <= stopPos.z ? c12 : c23);
}

void main()
{
	vec2 c = cellCoord;
	float p = texture2DRect(patchTex, c).r;
	if (p < 0.004) {
		gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
		return;
	}
	float m = texture2DRect(patchTex, c).g;
	vec2 g = vec2(texture2DRect(patchTex, c + vec2(1.0, 0.0)).r - texture2DRect(patchTex, c - vec2(1.0, 0.0)).r,
		texture2DRect(patchTex, c + vec2(0.0, 1.0)).r - texture2DRect(patchTex, c - vec2(0.0, 1.0)).r);
	float edge = smoothstep(0.02, 0.15, 0.5 * length(g));
	float tex = swirl(c);
	float mEff = clamp(m * (1.0 - edgeCool * edge) * (0.25 + 1.0 * tex), 0.0, 1.0);
	float bright = 0.45 + 0.75 * tex;
	float rim = rimGain * exp(-((p - 0.5) / 0.12) * ((p - 0.5) / 0.12));
	float a = min(1.0, p * 1.2) * brightness;
	gl_FragColor = vec4((ramp4(mEff) * bright + rim * stop1) * a, 1.0);
}
