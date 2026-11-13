// Prototype 2 (Mycorrhiza), style "Patches and hyphae": the lit ground, a
// light-liquid (MycorrhizaField::updateLiquid). Inside the presence mask a
// marbled swirl (curl-advected, two-level domain warp, ridges along smooth
// iso-contours, streaked along the curl), moved along the liquid's own
// velocity with a flow map, lit as a glossy surface (normals from dFdx/dFdy,
// diffuse + specular). Colour by maturity (cyan, blue-white, amber, orange)
// over deep-blue troughs, warmer where the liquid is deep, cooler at edges,
// thin rim; brightness proportional to presence. Drawn over black.
// Shares no code with heightMapShader. Keep in sync with ../GL2/patches.frag
// and with tools/mycorrhiza_harness/main.cpp (drawPatches, swirl, ramp4).
#version 150

uniform sampler2DRect patchTex; // per grid cell: R presence, G maturity
uniform sampler2DRect flowTex;  // per half cell (2 x 2 grid cells): R, G velocity (0.5 + v / (2 vMax)), B depth / hMax
uniform float time;         // field time, s
uniform float cellMM;       // mm per grid cell
uniform float scaleMM;      // texture scale
uniform float flowSpeed;    // slow drift of the swirl
uniform float contrast;
uniform float warp;         // domain-warp strength
uniform float streak;       // tap spacing along the curl direction
uniform float bands;        // marbling ridges per unit of the warped field
uniform float ridgeWidth;
uniform float flowCycle;    // s, flow-map phase length
uniform float vMax;         // mm/s, velocity encoding range
uniform float hMax;         // mm, depth encoding range
uniform float bump;         // relief of the swirl for lighting, in px
uniform float specGain;
uniform float shininess;
uniform float brightness;
uniform float edgeCool;     // edges shift toward the fresh (cool) colour
uniform float rimGain;      // thin brighter rim
uniform vec4 stopPos;       // ramp positions of the four stops
uniform vec3 stop0;         // cyan #3FD8FF
uniform vec3 stop1;         // blue-white #CFEFFF (also highlights and rim)
uniform vec3 stop2;         // amber #FFB347
uniform vec3 stop3;         // orange #F07A2A
uniform vec3 trough;        // deep blue #0A2A6E in the swirl's troughs

in vec2 cellCoord;
out vec4 outputColor;

// hash12 (Dave Hoskins), value noise, fbm; tools/mycorrhiza_harness has the same in C++.
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

// Two octaves, normalised to 0..1 (smooth: for the marbling ridges).
float fbm2(vec2 p)
{
	return (0.5 * vnoise(p) + 0.25 * vnoise(p * 2.02 + vec2(1.7, 9.2))) / 0.75;
}

// Swirl 0..1 at grid cell coordinate c (k: offset of the second flow-map phase):
// curl-advected, two-level domain warp; a soft body plus bright ridges along
// the iso-contours of a smooth field, averaged along the curl direction (marbling).
float swirl(vec2 c, float k)
{
	float t = time * flowSpeed;
	vec2 b = c * cellMM / scaleMM + k * vec2(17.3, 9.1);
	float e = 0.1;
	vec2 cp = b * 0.5 + vec2(3.1 + 0.05 * t, 7.7);
	vec2 curl = vec2(vnoise(cp + vec2(0.0, e)) - vnoise(cp - vec2(0.0, e)),
		-(vnoise(cp + vec2(e, 0.0)) - vnoise(cp - vec2(e, 0.0)))) / (2.0 * e);
	vec2 p = b + 0.5 * curl;
	vec2 q = vec2(fbm(p + vec2(0.10 * t, 0.0)), fbm(p + vec2(5.2, 1.3 - 0.08 * t)));
	vec2 r = vec2(fbm(p + warp * q + vec2(1.7 + 0.12 * t, 9.2)), fbm(p + warp * q + vec2(8.3, 2.8 - 0.10 * t)));
	float dl = length(curl);
	vec2 dir = dl > 1e-4 ? curl / dl : vec2(1.0, 0.0);
	vec2 u = p + warp * r;
	float v = fbm(u);
	float vr = (fbm2(u - dir * streak) + fbm2(u) + fbm2(u + dir * streak)) / 3.0;
	float body = clamp(0.5 + contrast * (v - 0.5), 0.0, 1.0);
	float tri = 1.0 - abs(2.0 * fract(vr * bands) - 1.0);
	float ridge = smoothstep(1.0 - ridgeWidth, 1.0, tri);
	return min(1.0, 0.3 + 0.4 * body + 0.45 * ridge);
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
	float p = texture(patchTex, c).r;
	float m = texture(patchTex, c).g;
	vec2 g = vec2(texture(patchTex, c + vec2(1.0, 0.0)).r - texture(patchTex, c - vec2(1.0, 0.0)).r,
		texture(patchTex, c + vec2(0.0, 1.0)).r - texture(patchTex, c - vec2(0.0, 1.0)).r);
	float edge = smoothstep(0.02, 0.15, 0.5 * length(g));
	vec3 fl = texture(flowTex, c * 0.5).rgb;
	vec2 vel = (fl.rg - 0.5) * 2.0 * vMax / cellMM;   // grid cells per second
	float depth = fl.b * hMax;
	// Flow map: two phases advected along the liquid's velocity, cross-faded.
	float ph0 = fract(time / flowCycle), ph1 = fract(time / flowCycle + 0.5);
	float blend = abs(1.0 - 2.0 * ph0);
	float tex = mix(swirl(c - vel * ph0 * flowCycle, 0.0), swirl(c - vel * ph1 * flowCycle, 1.0), blend);
	// Gloss: normals from the screen-space slope of the swirl (derivatives taken in every fragment).
	float hgt = tex * bump;
	vec3 n = normalize(vec3(-dFdx(hgt), -dFdy(hgt), 1.0));
	if (p < 0.004) {
		outputColor = vec4(0.0, 0.0, 0.0, 1.0);
		return;
	}
	vec3 L = normalize(vec3(-0.4, -0.5, 0.77));
	vec3 Hv = normalize(L + vec3(0.0, 0.0, 1.0));
	float diff = 0.6 + 0.4 * max(0.0, dot(n, L));
	float spec = specGain * pow(max(0.0, dot(n, Hv)), shininess);
	float deep = 0.75 + 0.45 * smoothstep(0.0, 0.5 * hMax, depth);
	float mEff = clamp(m * (1.0 - edgeCool * edge) * (0.35 + 0.9 * tex) * deep, 0.0, 1.0);
	vec3 col = mix(trough, ramp4(mEff), smoothstep(0.15, 0.6, tex));
	float bright = 0.35 + 0.85 * tex;
	float rim = rimGain * exp(-((p - 0.5) / 0.12) * ((p - 0.5) / 0.12));
	float a = min(1.0, p * 1.2) * brightness;
	outputColor = vec4((col * bright * diff + (spec + rim) * stop1) * a, 1.0);
}
