// Technique after "The Betta Fish in ASCII" by @dwcminh.ascii,
// https://github.com/dwcminh/ascii-art (BETTA-FISH, betta_fish.py; free for
// personal, non-commercial use). Our own GLSL; no code copied from it.
// AsciiFilter debug views, drawn full-screen instead of the characters, one
// flat colour per cell:
//   1 Bands: the cell's part R field (Sandbox: elevation band), a colour per
//     value; black where the cell has no part.
//   2 Edges: outline cells white, everything else black.
//   3 Lit: the brightness that chooses the pool (asciiDecide's s, after the
//     contrast stretch), grey, clipped to 0..1; black where there is no
//     coverage (cells left empty by the lit rules still show their s).
//   4 Pool: fill pool 0..3 as dark blue, teal, yellow, white; outlines and
//     marks dark grey; empty cells black.
// infoTex is asciiDecide's info output (infoPass 1; a: 1 drawn, 0.5 covered
// but empty, 0 no coverage).
// Keep in sync with ../GL2/asciiDebug.frag.
#version 150

uniform sampler2DRect infoTex;     // one texel per cell
uniform sampler2DRect partTex;     // one texel per cell
uniform vec2 cellSize;             // pixels (w, h)
uniform int view;                  // 1..4

in vec2 coord;                     // output pixel coordinate
out vec4 outputColor;

vec3 bandColour(float b)
{
	// 12 well-separated hues, then repeat; brightness alternates so
	// neighbouring bands differ in lightness as well.
	float h = fract(b * 0.4167);
	vec3 c = clamp(abs(fract(h + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0, 0.0, 1.0);
	return c * ((mod(b, 2.0) < 0.5) ? 0.95 : 0.6);
}

void main()
{
	vec2 cell = floor((floor(coord) + 0.5) / cellSize);
	vec4 info = texture(infoTex, cell + 0.5);
	vec3 c = vec3(0.0);
	if (view == 1) {
		vec4 q = floor(texture(partTex, cell + 0.5) * 255.0 + 0.5);
		if (q.a > 0.5)
			c = bandColour(q.r);
	} else if (view == 2) {
		c = vec3(info.b > 0.5 ? 1.0 : 0.0);
	} else if (view == 3) {
		c = vec3((info.a > 0.25) ? clamp(info.r, 0.0, 1.0) : 0.0);
	} else if (view == 4 && info.a > 0.75) {
		float lv = floor(info.g + 0.5);
		if (lv < -0.5)
			c = vec3(0.22);
		else if (lv < 0.5)
			c = vec3(0.10, 0.18, 0.55);
		else if (lv < 1.5)
			c = vec3(0.10, 0.60, 0.60);
		else if (lv < 2.5)
			c = vec3(0.90, 0.80, 0.20);
		else
			c = vec3(1.0);
	}
	outputColor = vec4(c, 1.0);
}
