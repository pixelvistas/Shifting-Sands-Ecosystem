// Prototype 4 (Scene), stylise step 2 (SceneStylizer), projector pixels.
// style 0 Off: the warped scene. 1 ASCII, shape-aware: per glyph cell,
// if the cell's edge strength (sceneEdges: structure tensor) exceeds
// edgeThreshold, a directional glyph by the stroke angle quantised to four
// screen directions (0 horizontal, 1 "\", 2 vertical, 3 "/"; y down),
// light or heavy by luminance (dirGlyph[dir * 2 + heavy]); otherwise the
// glyph whose measured ink coverage matches the cell's luminance (after
// the black point and luminanceGamma; ramp sorted by coverage on the
// CPU). Drawn as crisp coverage (>= 0.5) x the cell's scene colour x
// colourBoost; cells below the black point stay empty. debugEdges: edge
// cells in one colour per direction, the rest dim grey. 2 Dither: ordered
// dither on dotSize-pixel dots: with the palette (default) the dot's
// luminance is quantised to `levels` and the quantised value mapped
// through the 2 or 3 stops (every dot is exactly one palette tone: the
// two-tone look), otherwise each channel is quantised; with a Bayer threshold (4 x 4 or 8 x 8) fixed in projector
// space, or with lineScreen a horizontal line screen (threshold by pixel
// row, period linePeriod px: 1-pixel lines whose count encodes the tone);
// black stays black. 3 Both: the dither, with glyphs drawn over it in
// cells brighter than bothThreshold. Keep in sync with
// ../GL2/sceneStylize.frag.
#version 150

uniform sampler2DRect warpedTex;   // projector resolution, RGB scene
uniform sampler2DRect cellTex;     // per glyph cell: RGB mean, A luminance
uniform sampler2DRect edgeTex;     // per glyph cell: R edge strength, G stroke angle (deg)
uniform sampler2DRect atlasTex;    // AsciiFontAtlas: coverage in R
uniform int style;
uniform vec2 cellSize;             // projector pixels
uniform float rampGlyph[32];       // atlas indices, least to most ink
uniform float rampCount;
uniform float luminanceGamma;
uniform float blackPoint;
uniform float colourBoost;
uniform float dotSize;             // projector pixels
uniform float levels;              // per channel, >= 2
uniform int bayer8;                // 0: 4 x 4, 1: 8 x 8
uniform int paletteOn;
uniform float paletteStops;        // 2 or 3
uniform vec3 paletteColor[3];
uniform float bothThreshold;
uniform float edgeThreshold;       // luminance per pixel
uniform float dirGlyph[8];         // atlas indices: (dir x 2 + heavy)
uniform float weightSplit;         // heavy above this (gamma-mapped) luminance
uniform int debugEdges;
uniform vec3 debugColor[4];
uniform int lineScreen;
uniform float linePeriod;          // projector pixels

in vec2 coord;                     // projector pixel coordinate
out vec4 outputColor;

float bayer2(vec2 p)
{
	return mod(3.0 * p.y + 2.0 * p.x, 4.0);
}

float bayer4(vec2 c)
{
	vec2 p = mod(c, 4.0);
	return 4.0 * bayer2(mod(p, 2.0)) + bayer2(floor(p * 0.5));
}

// 8 x 8: 4 x M4(x mod 4, y mod 4) + M2(x/4 mod 2, y/4 mod 2); rank 0..63.
float bayer8f(vec2 c)
{
	vec2 p = mod(c, 8.0);
	return 4.0 * bayer4(mod(p, 4.0)) + bayer2(floor(p / 4.0));
}

float lum(vec3 c)
{
	return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

vec3 dither(vec2 pix)
{
	float ds = max(1.0, dotSize);
	vec2 dc = floor(pix / ds);
	vec3 sum = vec3(0.0);
	float n = 0.0;
	for (int y = 0; y < 8; y++) {
		if (float(y) >= ds)
			break;
		for (int x = 0; x < 8; x++) {
			if (float(x) >= ds)
				break;
			sum += texture(warpedTex, dc * ds + vec2(float(x), float(y)) + 0.5).rgb;
			n += 1.0;
		}
	}
	vec3 c = sum / max(1.0, n);
	float l = lum(c);
	if (l <= blackPoint)
		return vec3(0.0);
	float th = (lineScreen == 1) ? (mod(pix.y, max(1.0, linePeriod)) + 0.5) / max(1.0, linePeriod)
		: (bayer8 == 1) ? (bayer8f(dc) + 0.5) / 64.0 : (bayer4(dc) + 0.5) / 16.0;
	float top = max(1.0, levels - 1.0);
	if (paletteOn == 1) {
		float t = clamp(floor(clamp(l, 0.0, 1.0) * top + th) / top, 0.0, 1.0);
		if (t <= 0.0)
			return vec3(0.0);
		if (paletteStops < 2.5)
			return mix(paletteColor[0], paletteColor[1], t);
		return (t < 0.5) ? mix(paletteColor[0], paletteColor[1], t * 2.0) : mix(paletteColor[1], paletteColor[2], t * 2.0 - 1.0);
	}
	return clamp(floor(clamp(c, 0.0, 1.0) * top + th) / top, 0.0, 1.0);
}

// Glyph colour at pix (alpha 1) or nothing (alpha 0); minLum: cells at or
// below it draw no glyph.
vec4 glyph(vec2 pix, float minLum)
{
	vec2 cell = floor((pix + 0.5) / cellSize);
	vec2 local = pix - cell * cellSize;
	vec4 c = texture(cellTex, cell + 0.5);
	if (c.a <= minLum)
		return vec4(0.0);
	float l = pow(clamp((c.a - blackPoint) / max(1e-3, 1.0 - blackPoint), 0.0, 1.0), max(0.05, luminanceGamma));
	vec4 e = texture(edgeTex, cell + 0.5);
	float g;
	int dir = -1;
	if (e.r > edgeThreshold) {
		dir = int(mod(floor(e.g / 45.0 + 0.5), 4.0));
		g = dirGlyph[dir * 2 + ((l > weightSplit) ? 1 : 0)];
	} else {
		int idx = int(clamp(floor(l * (rampCount - 1.0) + 0.5), 0.0, rampCount - 1.0));
		g = rampGlyph[idx];
	}
	vec2 texel = vec2(g * cellSize.x + local.x + 0.5, local.y + 0.5);
	if (texture(atlasTex, texel).r < 0.5)
		return vec4(0.0);
	if (debugEdges == 1)
		return vec4((dir >= 0) ? debugColor[dir] : vec3(0.25), 1.0);
	return vec4(clamp(c.rgb * colourBoost, 0.0, 1.0), 1.0);
}

void main()
{
	vec2 pix = floor(coord);
	vec3 col;
	if (style == 1) {
		col = glyph(pix, blackPoint).rgb;
	} else if (style == 2) {
		col = dither(pix);
	} else if (style == 3) {
		vec4 g = glyph(pix, max(blackPoint, bothThreshold));
		col = (g.a > 0.5) ? g.rgb : dither(pix);
	} else {
		col = texture(warpedTex, pix + 0.5).rgb;
	}
	outputColor = vec4(col, 1.0);
}
