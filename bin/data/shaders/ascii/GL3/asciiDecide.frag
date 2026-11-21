// Technique after "The Betta Fish in ASCII" by @dwcminh.ascii,
// https://github.com/dwcminh/ascii-art (BETTA-FISH, betta_fish.py; free for
// personal, non-commercial use). Our own GLSL; no code copied from it.
// AsciiFilter pass 6: the character of each cell. Output: RGB = the glyph's
// colour (multiplied by its brightness, clipped to 0..1), A = the character
// code (0 = empty: drawn exact black).
// Brightness s: the cell's lit brightness (pass 3). Ground and life cells are
// contrast-stretched, s = (lit - litBlack) / (litWhite - litBlack); fluid
// cells keep their own rule's value. Then s x exposure. The colour source
// gives the hue only: rgb = colour / its brightest channel.
// 1. Mark (pass 4): the mark's glyph in the mark's own colour; never an
//    outline, never dimmed, never thinned.
// 2. Empty: no coverage (or under minCoverage).
// 3. Outline (pass 5), kept only on a run of strokes: the cells along the
//    stroke's own direction (the cell one step on, or the one beside it, so
//    stair steps count; '+' takes any neighbour) must hold strokes. minRun 1
//    keeps every stroke; 2 needs a stroke on one side; 3 needs one on both
//    sides, or two in a row on one side, so the shortest run kept is about
//    minRun cells. Outlines always draw (no emptiness, no thinning).
//    Brightness max(edgeFloor, s); colour paled towards white by edgePale.
//    strokeSmooth 1: the stroke is chosen again (same rule) from the sum of
//    the boundary directions of this cell and its 8 neighbours that are
//    outline cells, so a run along one line takes one character.
// 4. Fill. Ground Dark (ground 1): ground cells (parts G 0) draw only where
//    s >= ridge (lit ridges), life and fluid cells always draw. Ground Full:
//    s < emptyCut: empty; emptyCut <= s < thinCut: drawn only where the
//    cell's fixed hash < (s - emptyCut) / (thinCut - emptyCut), so letters
//    thin out towards the dark; s >= thinCut: drawn.
//    Pool = floor((s - fillStart) x fillScale) clipped to 0..3 (each pool
//    repeated to 12 in fillGlyph). Cell hash h = (7919 row + 104729 col) mod
//    9973 (integers); entry (h + floor(time x rate + h mod 11)) mod 12, rate
//    by parts G. Brightness clip(fillFloor + (1 - fillFloor) s / 0.9,
//    fillFloor, 1).
// 5. x (0.30 + 0.70 coverage^0.7) (not marks). Paling, as the reference:
//    rgb + (white - rgb) x clip((s - paleStart) x paleRate, 0, paleMax).
//    Colour = rgb x min(brightness, 1.4), clipped to 0..1.
// infoPass 1 (debug views) outputs instead: r = s, g = pool 0..3 (-1
// outline, -2 mark), b = 1 outline, a = 1 drawn, 0.5 covered but left empty
// (lit emptiness or thinning), 0 no coverage.
// Keep in sync with ../GL2/asciiDecide.frag.
#version 150

uniform sampler2DRect cellTex;     // pass 2: mean colour, coverage
uniform sampler2DRect partTex;     // pass 1
uniform sampler2DRect litTex;      // pass 3
uniform sampler2DRect markTex;     // pass 4
uniform sampler2DRect edgeTex;     // pass 5
uniform int marksOn;
uniform vec2 gridSize;             // cells
uniform float minCoverage;
uniform int minRun;                // 1..3
uniform int strokeSmooth;
uniform float edgeFloor;
uniform float edgePale;
uniform float litBlack;
uniform float litWhite;
uniform float exposure;
uniform int ground;                // 0 Full, 1 Dark
uniform float ridge;
uniform float emptyCut;
uniform float thinCut;
uniform float fillGlyph[48];       // 4 pools x 12 entries, character codes
uniform float fillFloor;
uniform float fillStart;
uniform float fillScale;
uniform float paleStart;
uniform float paleRate;
uniform float paleMax;
uniform float shimmerRate[3];      // steps per second, by shimmer class
uniform float time;                // seconds
uniform int infoPass;

in vec2 coord;                     // cell-grid coordinate
out vec4 outputColor;

// a mod b for non-negative integers a < 2^24 held in floats (exact).
float imod(float a, float b)
{
	return a - b * floor((a + 0.5) / b);
}

// Hash without sine (Dave Hoskins' hash12): a fixed value per cell.
float hash12(vec2 p)
{
	vec3 p3 = fract(vec3(p.xyx) * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}

bool strokeAt(vec2 n)
{
	if (n.x < 0.0 || n.y < 0.0 || n.x >= gridSize.x || n.y >= gridSize.y)
		return false;
	return texture(edgeTex, n + 0.5).r > 0.5 / 255.0;
}

// A stroke k steps along dir (or beside that cell: for an axis direction the
// cells either side of it, for a diagonal the two axis cells next to it).
bool along(vec2 c, vec2 dir, float k)
{
	vec2 a = c + dir * k;
	if (strokeAt(a))
		return true;
	if (dir.x == 0.0 || dir.y == 0.0) {
		vec2 side = vec2(dir.y, dir.x);
		return strokeAt(a + side) || strokeAt(a - side);
	}
	return strokeAt(a - vec2(dir.x, 0.0)) || strokeAt(a - vec2(0.0, dir.y));
}

void main()
{
	vec2 c = floor(coord);
	vec4 own = texture(cellTex, c + 0.5);
	vec4 q = floor(texture(partTex, c + 0.5) * 255.0 + 0.5);
	vec4 litc = texture(litTex, c + 0.5);
	bool fluid = litc.g > 0.5;
	float s = (litc.b > 0.5) ? litc.r : max(own.r, max(own.g, own.b)); // no normals: the colour's brightness
	if (!fluid && litc.b > 0.5)
		s = (s - litBlack) / max(litWhite - litBlack, 1e-3);
	s *= exposure;
	vec4 mark = (marksOn == 1) ? texture(markTex, c + 0.5) : vec4(0.0);
	float markGlyph = floor(mark.a * 255.0 + 0.5);
	if (markGlyph > 32.5 && max(mark.r, max(mark.g, mark.b)) > 0.0) {
		outputColor = (infoPass == 1) ? vec4(s, -2.0, 0.0, 1.0) : vec4(mark.rgb, markGlyph);
		return;
	}
	if (own.a <= 0.0 || own.a < minCoverage) {
		outputColor = vec4(0.0);
		return;
	}
	float m = max(own.r, max(own.g, own.b));
	vec3 rgb = (m > 0.0) ? own.rgb / m : vec3(0.0);

	// Outline, kept only on a run of strokes.
	float stroke = floor(texture(edgeTex, c + 0.5).r * 255.0 + 0.5);
	if (stroke > 32.5 && minRun > 1) {
		vec2 dir = vec2(1.0, 0.0);                           // _ -
		if (stroke == 124.0) dir = vec2(0.0, 1.0);           // |
		if (stroke == 47.0) dir = vec2(1.0, -1.0);           // / (y down)
		if (stroke == 92.0) dir = vec2(1.0, 1.0);            // backslash
		bool keep;
		if (stroke == 43.0) {                                // +: any neighbouring stroke
			keep = strokeAt(c + vec2(1.0, 0.0)) || strokeAt(c - vec2(1.0, 0.0)) || strokeAt(c + vec2(0.0, 1.0))
				|| strokeAt(c - vec2(0.0, 1.0)) || strokeAt(c + vec2(1.0, 1.0)) || strokeAt(c - vec2(1.0, 1.0))
				|| strokeAt(c + vec2(1.0, -1.0)) || strokeAt(c - vec2(1.0, -1.0));
		} else {
			bool f1 = along(c, dir, 1.0), b1 = along(c, -dir, 1.0);
			if (minRun == 2)
				keep = f1 || b1;
			else
				keep = (f1 && b1) || (f1 && along(c, dir, 2.0)) || (b1 && along(c, -dir, 2.0));
		}
		if (!keep)
			stroke = 0.0;
	}
	bool edge = stroke > 32.5;
	if (edge && strokeSmooth == 1 && stroke != 43.0) {
		vec2 g = vec2(0.0);
		for (int dy = -1; dy <= 1; dy++) {
			for (int dx = -1; dx <= 1; dx++) {
				vec2 n = c + vec2(float(dx), float(dy));
				if (strokeAt(n))
					g += floor(texture(edgeTex, n + 0.5).gb * 8.0 + 0.5) - 4.0;
			}
		}
		float ax = abs(g.x), ay = abs(g.y);
		if (ax + ay > 0.0) {
			if (ax > 2.0 * ay)
				stroke = 124.0;
			else if (ay > 2.0 * ax)
				stroke = (g.y > 0.0) ? 95.0 : 45.0;
			else
				stroke = (g.x * g.y > 0.0) ? 47.0 : 92.0;
		}
	}

	float glyph = 0.0, bright = 0.0, level = -1.0;
	bool drawn = true;
	if (edge) {
		glyph = stroke;
		bright = max(edgeFloor, s);
		rgb += (vec3(1.0) - rgb) * clamp(edgePale, 0.0, 1.0);
	} else {
		float cls = q.g;
		if (ground == 1) {
			drawn = cls > 0.5 || s >= ridge;
		} else if (s < emptyCut) {
			drawn = false;
		} else if (s < thinCut) {
			drawn = hash12(c + 31.0) < (s - emptyCut) / max(thinCut - emptyCut, 1e-3);
		}
		level = clamp(floor((s - fillStart) * fillScale), 0.0, 3.0);
		float h = imod(imod(c.y * 7919.0, 9973.0) + imod(c.x * 4999.0, 9973.0), 9973.0); // 104729 = 4999 (mod 9973)
		int k = int(clamp(cls, 0.0, 2.0));
		float steps = floor(time * shimmerRate[k] + imod(h, 11.0));
		glyph = fillGlyph[int(level * 12.0 + imod(h + steps, 12.0))];
		bright = clamp(fillFloor + (1.0 - fillFloor) * s / 0.9, fillFloor, 1.0);
	}
	if (infoPass == 1) {
		outputColor = vec4(s, level, edge ? 1.0 : 0.0, drawn ? 1.0 : 0.5);
		return;
	}
	if (!drawn) {
		outputColor = vec4(0.0);
		return;
	}
	bright *= 0.30 + 0.70 * pow(own.a, 0.7);
	rgb += (vec3(1.0) - rgb) * clamp((s - paleStart) * paleRate, 0.0, max(paleMax, 0.0));
	outputColor = vec4(clamp(rgb * min(bright, 1.4), 0.0, 1.0), glyph);
}
