// Technique after "The Betta Fish in ASCII" by @dwcminh.ascii,
// https://github.com/dwcminh/ascii-art (BETTA-FISH, betta_fish.py; free for
// personal, non-commercial use). Our own GLSL; no code copied from it.
// AsciiFilter pass 5 (2026-11-21): outline cells and their stroke, by the
// reference's rule (see asciiDecide.frag for the rest of the character).
// A neighbour "counts" if it is empty (no coverage; cells off the grid are
// empty), or (oneSided) it has another part that lies behind this one
// (parts R lower: the R field is the depth rank, higher = nearer), or a seam:
// another part at the same R whose identity (seamMode 0: G and B; 1: G only;
// 2: only where one side is fluid, G 2) differs and is higher, so the seam
// is drawn from one side; without oneSided any other part counts. g = the
// sum of the offsets to every counting neighbour of the 8, unweighted; the
// cell is an edge if a counting neighbour is one of the 4 side neighbours
// (sideOnly) or any of the 8. Stroke: '|' if |gx| > 2 |gy|; else '_' (gy > 0,
// boundary below) or '-' if |gy| > 2 |gx|; else '/' if gx gy > 0, '\' if
// not; '+' if g is exactly 0. Output R = the stroke's character code / 255
// (0: not an edge), G and B = (gx + 4) / 8 and (gy + 4) / 8 (each component
// is a whole number in -3..3), so asciiDecide can re-choose the stroke from
// a direction summed over neighbouring outline cells; marks are handled
// later (a mark replaces an outline).
// Keep in sync with ../GL2/asciiEdges.frag.
#version 150

uniform sampler2DRect cellTex;     // pass 2: A = coverage
uniform sampler2DRect partTex;     // pass 1
uniform vec2 gridSize;             // cells
uniform float minCoverage;
uniform int oneSided;
uniform int sideOnly;
uniform int seamMode;              // 0 G and B, 1 G only, 2 fluid only

in vec2 coord;                     // cell-grid coordinate
out vec4 outputColor;

bool emptyAt(vec2 n)
{
	if (n.x < 0.0 || n.y < 0.0 || n.x >= gridSize.x || n.y >= gridSize.y)
		return true;
	float a = texture(cellTex, n + 0.5).a;
	return a <= 0.0 || a < minCoverage;
}

// The part's identity for seams (see seamMode).
float identity(vec4 q)
{
	if (seamMode == 1)
		return q.g;
	if (seamMode == 2)
		return (q.g > 1.5) ? 1.0 : 0.0;
	return q.g * 256.0 + q.b;
}

void main()
{
	vec2 c = floor(coord);
	if (emptyAt(c)) {
		outputColor = vec4(0.0);
		return;
	}
	vec4 q = floor(texture(partTex, c + 0.5) * 255.0 + 0.5);
	float ident = identity(q);
	vec2 g = vec2(0.0);
	bool edge = false;
	for (int dy = -1; dy <= 1; dy++) {
		for (int dx = -1; dx <= 1; dx++) {
			if (dx == 0 && dy == 0)
				continue;
			vec2 d = vec2(float(dx), float(dy));
			bool counts = emptyAt(c + d);
			if (!counts) {
				vec4 qn = floor(texture(partTex, c + d + 0.5) * 255.0 + 0.5);
				float identN = identity(qn);
				bool deeper = qn.r < q.r;
				bool seam = qn.r == q.r && identN != ident;
				if (oneSided == 1)
					counts = deeper || (seam && ident < identN);
				else
					counts = qn.r != q.r || seam;
			}
			if (counts) {
				g += d;
				if (sideOnly == 0 || dx == 0 || dy == 0)
					edge = true;
			}
		}
	}
	float glyph = 0.0;
	if (edge) {
		float ax = abs(g.x), ay = abs(g.y);
		if (ax + ay == 0.0)
			glyph = 43.0;                              // +
		else if (ax > 2.0 * ay)
			glyph = 124.0;                             // |
		else if (ay > 2.0 * ax)
			glyph = (g.y > 0.0) ? 95.0 : 45.0;         // _ or -
		else
			glyph = (g.x * g.y > 0.0) ? 47.0 : 92.0;   // / or backslash
	}
	outputColor = vec4(glyph / 255.0, (g + 4.0) / 8.0, 1.0);
}
