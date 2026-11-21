// Technique after "The Betta Fish in ASCII" by @dwcminh.ascii,
// https://github.com/dwcminh/ascii-art (BETTA-FISH, betta_fish.py; free for
// personal, non-commercial use). Our own GLSL; no code copied from it.
// AsciiFilter pass 4: the character of each cell, by the reference's rules.
// Output: RGB = the glyph's colour (multiplied by its brightness, clipped to
// 0..1), A = the character code (0 = empty: drawn exact black).
// Brightness L = the cell colour's brightest channel; rgb = colour / L (so
// rgb x L is the input colour: the 2D stand-in for the reference's separate
// lighting value and colour).
// 1. Mark (pass 3): the mark's glyph in the mark's own colour; never an
//    outline, never dimmed by coverage.
// 2. Empty: coverage 0 (or under minCoverage, if set).
// 3. Outline. A neighbour "counts" if it is empty (cells off the grid are
//    empty), or (oneSided) it has another part that lies behind this one
//    (parts R lower: the R field is the depth rank, higher = nearer), or a
//    seam: another part at the same R whose identity (G, B) is higher, so
//    the seam is drawn from one side; without oneSided any other part
//    counts. g = the sum of the offsets to every counting neighbour of the
//    8, unweighted; the cell is an edge if a counting neighbour is one of
//    the 4 side neighbours (sideOnly) or any of the 8. Character: '|' if
//    |gx| > 2 |gy|; else '_' (gy > 0, boundary below) or '-' if |gy| > 2 |gx|;
//    else '/' if gx gy > 0, '\' if not; '+' if g is exactly 0.
//    Brightness max(edgeFloor, L).
// 4. Fill. Level = floor((L - fillStart) x fillScale) clipped to 0..3 picks
//    a pool (dark to bright, each repeated to 12 entries in fillGlyph). Cell
//    hash h = (7919 row + 104729 col) mod 9973 (integers). Entry =
//    (h + floor(time x rate + h mod 11)) mod 12: every cell of a shimmer
//    class steps at `rate` per second, at the same instants, each from its
//    own place. rate by the part's G (shimmer class 0, 1, 2).
//    Brightness clip(fillFloor + (1 - fillFloor) L / 0.9, fillFloor, 1).
// 5. Edges and fill: brightness x (0.30 + 0.70 coverage^0.7); colour =
//    rgb x min(brightness, 1.4), clipped to 0..1.
// Keep in sync with ../GL2/asciiDecide.frag.
#version 150

uniform sampler2DRect cellTex;     // pass 2: mean colour, coverage
uniform sampler2DRect partTex;     // pass 1: the part of each cell
uniform sampler2DRect markTex;     // pass 3: brightest mark
uniform int marksOn;
uniform vec2 gridSize;             // cells
uniform float minCoverage;
uniform float edgeFloor;
uniform int oneSided;
uniform int sideOnly;
uniform float fillGlyph[48];       // 4 pools x 12 entries, character codes
uniform float fillFloor;
uniform float fillStart;
uniform float fillScale;
uniform float shimmerRate[3];      // steps per second, by shimmer class
uniform float time;                // seconds

in vec2 coord;                     // cell-grid coordinate
out vec4 outputColor;

// a mod b for non-negative integers a < 2^24 held in floats (exact).
float imod(float a, float b)
{
	return a - b * floor((a + 0.5) / b);
}

bool emptyAt(vec2 n)
{
	if (n.x < 0.0 || n.y < 0.0 || n.x >= gridSize.x || n.y >= gridSize.y)
		return true;
	float a = texture(cellTex, n + 0.5).a;
	return a <= 0.0 || a < minCoverage;
}

void main()
{
	vec2 c = floor(coord);
	vec4 own = texture(cellTex, c + 0.5);
	vec4 mark = (marksOn == 1) ? texture(markTex, c + 0.5) : vec4(0.0);
	float markGlyph = floor(mark.a * 255.0 + 0.5);
	if (markGlyph > 32.5 && max(mark.r, max(mark.g, mark.b)) > 0.0) {
		outputColor = vec4(mark.rgb, markGlyph);
		return;
	}
	if (own.a <= 0.0 || own.a < minCoverage) {
		outputColor = vec4(0.0);
		return;
	}
	vec4 q = floor(texture(partTex, c + 0.5) * 255.0 + 0.5);
	float ident = q.g * 256.0 + q.b;
	vec2 g = vec2(0.0);
	bool edge = false;
	for (int dy = -1; dy <= 1; dy++) {
		for (int dx = -1; dx <= 1; dx++) {
			if (dx == 0 && dy == 0)
				continue;
			vec2 d = vec2(float(dx), float(dy));
			bool empty = emptyAt(c + d);
			bool counts = empty;
			if (!empty) {
				vec4 qn = floor(texture(partTex, c + d + 0.5) * 255.0 + 0.5);
				bool other = any(notEqual(qn.rgb, q.rgb));
				if (oneSided == 1) {
					float identN = qn.g * 256.0 + qn.b;
					counts = other && (qn.r < q.r || (qn.r == q.r && ident < identN));
				} else {
					counts = other;
				}
			}
			if (counts) {
				g += d;
				if (sideOnly == 0 || dx == 0 || dy == 0)
					edge = true;
			}
		}
	}
	float L = max(own.r, max(own.g, own.b));
	vec3 rgb = (L > 0.0) ? own.rgb / L : vec3(0.0);
	float glyph, bright;
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
		bright = max(edgeFloor, L);
	} else {
		float level = clamp(floor((L - fillStart) * fillScale), 0.0, 3.0);
		float h = imod(imod(c.y * 7919.0, 9973.0) + imod(c.x * 4999.0, 9973.0), 9973.0); // 104729 = 4999 (mod 9973)
		int cls = int(clamp(q.g, 0.0, 2.0));
		float steps = floor(time * shimmerRate[cls] + imod(h, 11.0));
		float i = imod(h + steps, 12.0);
		glyph = fillGlyph[int(level * 12.0 + i)];
		bright = clamp(fillFloor + (1.0 - fillFloor) * L / 0.9, fillFloor, 1.0);
	}
	bright *= 0.30 + 0.70 * pow(own.a, 0.7);
	outputColor = vec4(clamp(rgb * min(bright, 1.4), 0.0, 1.0), glyph);
}
