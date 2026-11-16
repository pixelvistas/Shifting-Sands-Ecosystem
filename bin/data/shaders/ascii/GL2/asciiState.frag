// Prototype 3 (ASCII), pass 2: per glyph cell, smoothing over time and
// three levels held with hysteresis (ping-pong: prevTex is last frame's
// output). Keep in sync with ../GL3/asciiState.frag; tools/ascii_preview
// runs this file.
//
// sampleTex: pass 1 (R value, A valid). prevTex / output:
//   R smoothed value v (0..1)
//   G held glyph level (0 .. levels - 1)
//   B held band (0 .. bands - 1) + bands x (held contour index + 512)
//   A 1 valid (0: no data; start fresh when data returns)
// v = mix(previous v, sample, alpha), alpha = 1 - exp(-dt / T).
// Each level is held by the same rule: with x the level coordinate, the
// held level k stays while k - hysteresis <= x < k + 1 + hysteresis, and
// otherwise becomes floor(x) (clamped):
//   glyph:   x = clamp(v) x (levels - 1) + d
//   band:    x = clamp(v) x (bands - 1) + d
//   contour: x = (fieldLo + clamp(v) x fieldSpan - contourOffset) / contourInterval
// d = the cell's 4 x 4 Bayer threshold, (rank + 0.5) / 16, or 0.5 without
// dithering. Contours are not dithered.
#version 120
#extension GL_ARB_texture_rectangle : enable

uniform sampler2DRect sampleTex;
uniform sampler2DRect prevTex;
uniform float alpha;
uniform float levels;
uniform float bands;
uniform float hysteresis;      // in steps of each level
uniform int dither;
uniform int resetAll;
uniform float fieldLo;         // field units (mm) at value 0
uniform float fieldSpan;       // field units (mm) from value 0 to 1
uniform float contourInterval; // field units (mm), > 0
uniform float contourOffset;   // field units (mm)

varying vec2 coord;

// Rank 0..15 of the 4 x 4 Bayer matrix
//   0  8  2 10 / 12  4 14  6 / 3 11  1  9 / 15  7 13  5  (rows = y)
// built from the 2 x 2 matrix 0 2 / 3 1: M4 = 4 M2(x, y) + M2(x/2, y/2).
float bayer2(vec2 p)
{
	return mod(3.0 * p.y + 2.0 * p.x, 4.0);
}

float bayer4(vec2 cell)
{
	vec2 p = mod(cell, 4.0);
	return 4.0 * bayer2(mod(p, 2.0)) + bayer2(floor(p * 0.5));
}

// The held level for coordinate x, given the previous level k (range kMin .. kMax).
float holdLevel(float x, float k, float kMin, float kMax, bool fresh)
{
	if (!fresh && x >= k - hysteresis && x < k + 1.0 + hysteresis)
		return k;
	return clamp(floor(x), kMin, kMax);
}

void main()
{
	vec4 s = texture2DRect(sampleTex, coord);
	if (s.a < 0.5) {
		gl_FragColor = vec4(0.0);
		return;
	}
	vec4 p = texture2DRect(prevTex, coord);
	bool fresh = resetAll == 1 || p.a < 0.5;
	float v = fresh ? s.r : mix(p.r, s.r, alpha);
	float vc = clamp(v, 0.0, 1.0);
	float d = (dither == 1) ? (bayer4(floor(coord)) + 0.5) / 16.0 : 0.5;

	float g = holdLevel(vc * (levels - 1.0) + d, p.g, 0.0, levels - 1.0, fresh);

	float packedQ = floor(p.b / bands);
	float prevBand = p.b - bands * packedQ;
	float b = holdLevel(vc * (bands - 1.0) + d, prevBand, 0.0, bands - 1.0, fresh);

	float xc = (fieldLo + vc * fieldSpan - contourOffset) / contourInterval;
	float q = holdLevel(xc, packedQ - 512.0, -511.0, 511.0, fresh);

	gl_FragColor = vec4(v, g, b + bands * (q + 512.0), 1.0);
}
