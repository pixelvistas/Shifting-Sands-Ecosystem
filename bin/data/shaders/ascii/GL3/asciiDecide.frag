// AsciiFilter pass 4: the character of each cell. Output: RGB = the
// glyph's colour (already multiplied by its brightness), A = the
// character code (0 = empty: drawn exact black).
// 1. A cell with a mark (pass 3) draws the mark's glyph in the mark's
//    colour, and is never an outline.
// 2. Coverage (pass 1 A) below minCoverage: empty.
// 3. Outline: a neighbour (8-neighbourhood, inside the grid) that is empty
//    or has a different part (pass 2) makes the cell an edge (oneSided:
//    a different part counts only if its key is lower, so a boundary
//    between two parts is drawn on one side only; empty neighbours always
//    count). g = the sum of the unit directions to those neighbours (y
//    down). Character: '+' if |g| cancels (both components < 0.25); '|'
//    if mostly horizontal (|gx| > 2.414 |gy|); '_' (boundary below) or '-'
//    (boundary above) if mostly vertical; otherwise '/' if gx * gy > 0,
//    '\' if < 0. The edge colour is raised so that its brightest channel
//    is at least edgeFloor.
// 4. Fill: a pool by luminance (4 equal bins of 0..1, dark to bright),
//    and within the pool (fixed hash of the cell + floor(time x rate +
//    second hash)) mod pool length: each cell steps to the next letter
//    `rate` times a second, at its own phase. rate by the part's G field
//    (shimmer class 0, 1, 2).
// 5. Brightness: x (0.30 + 0.70 x coverage^0.7), outlines included.
// Keep in sync with ../GL2/asciiDecide.frag.
#version 150

uniform sampler2DRect cellTex;     // pass 1: mean colour, coverage
uniform sampler2DRect partTex;     // pass 2: dominant part
uniform sampler2DRect markTex;     // pass 3: brightest mark
uniform int marksOn;
uniform vec2 gridSize;             // cells
uniform float minCoverage;
uniform float edgeFloor;
uniform int oneSided;
uniform float poolGlyph[48];       // character codes, pools one after another
uniform float poolStart[4];
uniform float poolLen[4];
uniform float shimmerRate[3];      // steps per second, by shimmer class
uniform float time;                // seconds

in vec2 coord;                     // cell-grid coordinate
out vec4 outputColor;

float lum(vec3 c)
{
	return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// Hash without sine (Dave Hoskins' hash12): stable per cell.
float hash12(vec2 p)
{
	vec3 p3 = fract(vec3(p.xyx) * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}

float partKey(vec4 p)
{
	vec4 q = floor(p * 255.0 + 0.5);
	return (q.a < 0.5) ? -1.0 : q.r * 65536.0 + q.g * 256.0 + q.b;
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
	if (own.a < minCoverage) {
		outputColor = vec4(0.0);
		return;
	}
	vec4 part = texture(partTex, c + 0.5);
	float k = partKey(part);
	vec2 g = vec2(0.0);
	bool edge = false;
	for (int dy = -1; dy <= 1; dy++) {
		for (int dx = -1; dx <= 1; dx++) {
			if (dx == 0 && dy == 0)
				continue;
			vec2 d = vec2(float(dx), float(dy));
			vec2 n = c + d;
			if (n.x < 0.0 || n.y < 0.0 || n.x >= gridSize.x || n.y >= gridSize.y)
				continue;
			bool empty = texture(cellTex, n + 0.5).a < minCoverage;
			float kn = partKey(texture(partTex, n + 0.5));
			bool differ = !empty && kn != k && (oneSided == 0 || kn < k);
			if (empty || differ) {
				edge = true;
				g += (dx != 0 && dy != 0) ? d * 0.70710678 : d;
			}
		}
	}
	vec3 col = own.rgb;
	float glyph;
	if (edge) {
		float ax = abs(g.x), ay = abs(g.y);
		if (ax < 0.25 && ay < 0.25)
			glyph = 43.0;                              // +
		else if (ax > 2.4142 * ay)
			glyph = 124.0;                             // |
		else if (ay > 2.4142 * ax)
			glyph = (g.y > 0.0) ? 95.0 : 45.0;         // _ or -
		else
			glyph = (g.x * g.y > 0.0) ? 47.0 : 92.0;   // / or backslash
		float m = max(col.r, max(col.g, col.b));
		if (m > 1e-5)
			col = min(col * max(1.0, edgeFloor / m), vec3(1.0));
	} else {
		int pool = int(clamp(floor(lum(col) * 4.0), 0.0, 3.0));
		float len = max(1.0, poolLen[pool]);
		int cls = int(clamp(floor(part.g * 255.0 + 0.5), 0.0, 2.0));
		float steps = floor(time * shimmerRate[cls] + hash12(c + 17.0));
		float i = mod(floor(hash12(c) * len) + steps, len);
		glyph = poolGlyph[int(poolStart[pool] + i)];
	}
	col *= 0.30 + 0.70 * pow(own.a, 0.7);
	outputColor = vec4(col, glyph);
}
