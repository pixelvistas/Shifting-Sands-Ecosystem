// Technique after "The Betta Fish in ASCII" by @dwcminh.ascii,
// https://github.com/dwcminh/ascii-art (BETTA-FISH, betta_fish.py; free for
// personal, non-commercial use). Our own GLSL; no code copied from it.
// AsciiFilter pass 4: the mark of each cell = the brightest marked pixel
// anywhere in the cell (every pixel is read, so a 1-px mark is never
// missed). Output: that pixel of the marks buffer (RGB colour, A glyph),
// or 0 if the cell has no mark. Cells are at most 16 x 16 pixels.
// Keep in sync with ../GL2/asciiMarks.frag.
#version 150

uniform sampler2DRect marksTex;    // marks buffer, in pixels
uniform vec2 srcSize;              // pixels
uniform vec2 cellSize;             // pixels (w, h), <= 16

in vec2 coord;                     // cell-grid coordinate
out vec4 outputColor;

void main()
{
	vec2 origin = floor(coord) * cellSize;
	vec4 best = vec4(0.0);
	float bestB = 0.0;
	for (int y = 0; y < 16; y++) {
		if (float(y) >= cellSize.y)
			break;
		for (int x = 0; x < 16; x++) {
			if (float(x) >= cellSize.x)
				break;
			vec2 p = origin + vec2(float(x), float(y));
			if (p.x < srcSize.x && p.y < srcSize.y) {
				vec4 m = texture(marksTex, p + 0.5);
				float b = max(m.r, max(m.g, m.b));
				if (m.a > 0.0 && b > bestB) {
					bestB = b;
					best = m;
				}
			}
		}
	}
	outputColor = best;
}
