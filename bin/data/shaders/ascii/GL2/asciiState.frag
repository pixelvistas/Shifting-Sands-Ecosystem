// Prototype 3 (ASCII), pass 2: per glyph cell, smoothing over time and
// the held glyph level (ping-pong: prevTex is last frame's output).
// Keep in sync with ../GL3/asciiState.frag; tools/ascii_preview runs this
// file.
//
// sampleTex: pass 1 (R value, A valid). prevTex / output: R smoothed
// value, G held level (0 .. levels - 1), A 1 valid (0: no data, start
// fresh when data returns).
//   v = mix(previous v, sample, alpha)       (alpha = 1 - exp(-dt / T))
//   x = clamp(v, 0, 1) x (levels - 1),  d = Bayer 4 x 4 threshold of the
//   cell, (rank + 0.5) / 16, or 0.5 without dithering
//   held level g stays while g - hysteresis <= x + d < g + 1 + hysteresis,
//   otherwise g = floor(x + d) (clamped to 0 .. levels - 1).
#version 120
#extension GL_ARB_texture_rectangle : enable

uniform sampler2DRect sampleTex;
uniform sampler2DRect prevTex;
uniform float alpha;
uniform float levels;
uniform float hysteresis; // ramp steps
uniform int dither;
uniform int resetAll;

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
	float top = levels - 1.0;
	float d = (dither == 1) ? (bayer4(floor(coord)) + 0.5) / 16.0 : 0.5;
	float xd = clamp(v, 0.0, 1.0) * top + d;
	float g = clamp(floor(xd), 0.0, top);
	if (!fresh && xd >= p.g - hysteresis && xd < p.g + 1.0 + hysteresis)
		g = p.g;
	gl_FragColor = vec4(v, g, 0.0, 1.0);
}
