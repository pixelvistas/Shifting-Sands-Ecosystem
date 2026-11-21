// AsciiFilter pass 2: the part of each cell. The parts source holds, per
// pixel, three part fields in R, G, B (8-bit values; the caller decides
// what they mean) and A = 1 where anything was drawn (0: no part). The
// cell is sampled at its centre pixel and at the same 4 x 4 points as
// pass 1. Output: the dominant sample, i.e. the value shared by the most
// samples; the centre counts twice and is tried first, so it wins ties
// and is kept wherever it is not outvoted. "No part" never wins while any
// sample has a part (a cell on the edge of the drawn area takes the part
// that is there); A = 0 only if no sample has one.
// Keep in sync with ../GL2/asciiParts.frag.
#version 150

uniform sampler2DRect partsTex;    // parts source, in pixels
uniform vec2 srcSize;              // pixels
uniform vec2 cellSize;             // pixels (w, h)

in vec2 coord;                     // cell-grid coordinate
out vec4 outputColor;

// One number per part value (exact in a float: < 2^24); -1 = no part.
float partKey(vec4 p)
{
	vec4 q = floor(p * 255.0 + 0.5);
	return (q.a < 0.5) ? -1.0 : q.r * 65536.0 + q.g * 256.0 + q.b;
}

void main()
{
	vec2 origin = floor(coord) * cellSize;
	vec2 lim = srcSize - 1.0;
	vec4 v[17];
	float key[17];
	v[0] = texture(partsTex, min(floor(origin + 0.5 * cellSize), lim) + 0.5);
	for (int y = 0; y < 4; y++) {
		for (int x = 0; x < 4; x++) {
			vec2 p = min(floor(origin + (vec2(float(x), float(y)) + 0.5) * 0.25 * cellSize), lim);
			v[1 + y * 4 + x] = texture(partsTex, p + 0.5);
		}
	}
	for (int i = 0; i < 17; i++)
		key[i] = partKey(v[i]);
	int best = 0;
	float bestN = -1.0;
	for (int i = 0; i < 17; i++) {
		float n = (key[i] < 0.0) ? -100.0 : 0.0;
		for (int j = 0; j < 17; j++)
			n += (key[j] == key[i]) ? ((j == 0) ? 2.0 : 1.0) : 0.0;
		if (n > bestN) {
			bestN = n;
			best = i;
		}
	}
	vec4 b = v[0];
	float bk = key[0];
	for (int i = 1; i < 17; i++)
		if (i == best) {
			b = v[i];
			bk = key[i];
		}
	outputColor = (bk < 0.0) ? vec4(0.0) : vec4(b.rgb, 1.0);
}
