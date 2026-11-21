// Technique after "The Betta Fish in ASCII" by @dwcminh.ascii,
// https://github.com/dwcminh/ascii-art (BETTA-FISH, betta_fish.py; free for
// personal, non-commercial use). Our own GLSL; no code copied from it.
// AsciiFilter pass 5: per output pixel, the cell's character (pass 4) from
// the font atlas (ASCII 32..126, 16 per row; each slot is the cell with a
// margin of atlasPad pixels all round, and only the cell is read, so what a
// character draws beyond its cell is cropped, as in the reference; R =
// glyph coverage), times the cell's colour. Output A = 1 in every pixel of
// a drawn cell, 0 in empty cells (the glow mask).
// Keep in sync with ../GL2/asciiGlyphs.frag.
#version 150

uniform sampler2DRect decideTex;   // pass 4, one texel per cell
uniform sampler2DRect atlasTex;    // font atlas, in pixels
uniform vec2 cellSize;             // pixels (w, h)
uniform float atlasPad;            // slot margin, pixels

in vec2 coord;                     // output pixel coordinate
out vec4 outputColor;

void main()
{
	vec2 p = floor(coord);
	vec2 cell = floor((p + 0.5) / cellSize);
	vec2 local = p - cell * cellSize;
	vec4 d = texture(decideTex, cell + 0.5);
	float code = floor(d.a + 0.5);
	if (code < 32.5 || code > 126.5) {
		outputColor = vec4(0.0);
		return;
	}
	float slot = code - 32.0;
	vec2 slotOrigin = vec2(mod(slot, 16.0), floor((slot + 0.5) / 16.0)) * (cellSize + 2.0 * atlasPad) + atlasPad;
	float a = texture(atlasTex, slotOrigin + local + 0.5).r;
	outputColor = vec4(d.rgb * a, 1.0);
}
