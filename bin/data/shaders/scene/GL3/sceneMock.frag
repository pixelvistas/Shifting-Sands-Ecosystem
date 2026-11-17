// Prototype 4 (Scene), mock terrain (display only, "Mock terrain" on):
// a procedural heightmap at grid resolution, standing in for the Kinect.
// part 0: R = height (mm) + optional per-frame noise (uniform, +-noiseMM,
//   hashed per cell and frame, for the noise test).
// part 1: water below "Mock water level" (mm): R depth (mm), G/B flow
//   velocity (0), A d(depth)/dt (mm/s) from the noise-free height at
//   time and time - dt (so the mock hand's dig makes ripples).
// Terrain (fractions of the grid): flat sand at 9 mm, a mound (+55 mm),
// a ridge (+30 mm) and a basin (-40 mm), or flat sand only (flatTerrain,
// for the noise test); the optional hand is a 20 mm deep
// dimple (radius 25 mm) circling the basin's rim. Keep in sync with
// ../GL2/sceneMock.frag; tools/scene_preview runs this file.
#version 150

uniform int part;
uniform vec2 gridSize;       // cells
uniform float mmPerCell;
uniform float time;          // s (animation clock)
uniform float dt;            // s
uniform float waterLevel;    // mm
uniform float noiseMM;
uniform float frame;
uniform int handOn;
uniform float handSpeed;     // turns per minute
uniform int flatTerrain;

in vec2 coord;               // grid texel coordinate
out vec4 outputColor;

float hash12(vec2 p)
{
	vec3 p3 = fract(vec3(p.xyx) * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}

float gauss(float d, float s)
{
	return exp(-0.5 * d * d / (s * s));
}

float segDist(vec2 p, vec2 a, vec2 b)
{
	vec2 ab = b - a;
	float t = clamp(dot(p - a, ab) / dot(ab, ab), 0.0, 1.0);
	return length(p - a - ab * t);
}

float mockHeight(vec2 g, float t)
{
	vec2 aspect = vec2(gridSize.x / gridSize.y, 1.0);
	vec2 p = g / gridSize;
	vec2 q = p * aspect;
	float h = 9.0;
	if (flatTerrain == 1)
		return h;
	h += 55.0 * gauss(length(q - vec2(0.25, 0.45) * aspect), 0.12);
	h += 30.0 * gauss(segDist(q, vec2(0.45, 0.15) * aspect, vec2(0.85, 0.35) * aspect), 0.045);
	h -= 40.0 * gauss(length(q - vec2(0.66, 0.68) * aspect), 0.13);
	if (handOn == 1) {
		float a = t * handSpeed / 60.0 * 6.2831853;
		vec2 c = vec2(0.66, 0.68) * gridSize + vec2(cos(a), sin(a)) * 0.15 * gridSize.y;
		h -= 20.0 * gauss(length(g - c) * mmPerCell, 25.0);
	}
	return h;
}

void main()
{
	vec2 g = coord;
	if (part == 0) {
		float n = (noiseMM > 0.0) ? (hash12(floor(g) + vec2(frame * 7.13, frame * 3.71)) * 2.0 - 1.0) * noiseMM : 0.0;
		outputColor = vec4(mockHeight(g, time) + n, 0.0, 0.0, 1.0);
	} else {
		float d = max(0.0, waterLevel - mockHeight(g, time));
		float dPrev = max(0.0, waterLevel - mockHeight(g, time - dt));
		outputColor = vec4(d, 0.0, 0.0, (dt > 0.0) ? (d - dPrev) / dt : 0.0);
	}
}
