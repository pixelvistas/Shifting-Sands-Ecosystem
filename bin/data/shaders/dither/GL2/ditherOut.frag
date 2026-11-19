// DitherFilter pass 2: per output pixel. The pixel's dot (pass 1: mean
// pre-filtered colour, A = its luminance) is quantised with a threshold
// fixed in output pixels (no time term):
//   pattern 0 Bayer 4 x 4, 1 Bayer 8 x 8 (both per dot); 2 horizontal line
//   screen (per pixel row, period linePeriod px); 3 clustered-dot halftone
//   (per dot: a round spot per cell of halftoneCell x halftoneCell dots,
//   grid rotated by the halftone angle; threshold = 1 - the cell's area
//   fraction closer to the spot centre than this dot, so dots grow from
//   the centre and tone is linear in the area).
// mode 0 hue-preserving: luminance quantised to `levels`, output = the
//   dot's own hue (c / max(c)) at that luminance (hue x tq / lum(hue),
//   then divided by its maximum channel if that exceeds 1).
// mode 1 per channel: each channel quantised to `levels`.
// mode 2 palette: quantised luminance mapped through 2 or 3 stops.
// In every mode a dot whose luminance is at or below blackPoint is black.
// Line protection (lineMode 1 bright, 2 bright and dark): luminance is
// sampled lineReach px away on both sides along 4 axes (horizontal,
// vertical, 2 diagonals). Ridge across axis i = how much the pixel exceeds
// both samples across it, less how much it also exceeds both samples
// along the perpendicular axis (a line continues along itself; a point or
// a fine texture such as the plant grain stands out both ways and scores
// ~0). A pixel whose best ridge exceeds lineThreshold (thin bright line)
// - or, with mode 2, whose valley does (thin dark line) - is drawn as its
// own pre-filtered colour, undithered. Wide areas and their edges are not
// above (or below) both sides, so they stay dithered.
// Pre-filter: identical to ditherDots.frag.
// Keep in sync with ../GL3/ditherOut.frag.
#version 120
#extension GL_ARB_texture_rectangle : enable

uniform sampler2DRect srcTex;      // source, in pixels
uniform sampler2DRect dotTex;      // pass 1, one texel per dot
uniform vec2 srcSize;              // pixels
uniform float dotSize;             // pixels, 1..8
uniform float exposure;
uniform float contrast;
uniform float saturation;
uniform int mode;                  // 0 hue, 1 per channel, 2 palette
uniform int pattern;               // 0 Bayer 4, 1 Bayer 8, 2 line screen, 3 halftone
uniform float levels;              // >= 2
uniform float blackPoint;          // luminance
uniform float linePeriod;          // pixels
uniform vec2 halftoneRot;          // (cos, sin) of the screen angle
uniform float halftoneCell;        // dots per cell side
uniform float paletteStops;        // 2 or 3
uniform vec3 paletteColor[3];
uniform int lineMode;              // 0 off, 1 bright, 2 bright and dark
uniform float lineThreshold;       // luminance
uniform float lineReach;           // pixels

varying vec2 coord;                     // source pixel coordinate

float lum(vec3 c)
{
	return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

vec3 prefilter(vec3 c)
{
	c = max(c, vec3(0.0)) * exposure;
	float l = lum(c);
	c = max(vec3(l) + (c - vec3(l)) * saturation, vec3(0.0));
	float ls = lum(c);
	float lt = max((l - 0.5) * contrast + 0.5, 0.0);
	c = (ls > 1e-5) ? c * (lt / ls) : vec3(0.0);
	float m = max(c.r, max(c.g, c.b));
	return (m > 1.0) ? c / m : c;
}

float bayer2(vec2 p)
{
	return mod(3.0 * p.y + 2.0 * p.x, 4.0);
}

// Rank 0..15 (as SceneStylizer's sceneStylize.frag).
float bayer4(vec2 c)
{
	vec2 p = mod(c, 4.0);
	return 4.0 * bayer2(mod(p, 2.0)) + bayer2(floor(p * 0.5));
}

// Rank 0..63.
float bayer8f(vec2 c)
{
	vec2 p = mod(c, 8.0);
	return 4.0 * bayer4(mod(p, 4.0)) + bayer2(floor(p / 4.0));
}

// Share of a unit square (centred on the spot) closer than d to its centre.
float spotArea(float d)
{
	if (d <= 0.5)
		return 3.14159265 * d * d;
	if (d >= 0.70710678)
		return 1.0;
	float seg = d * d * acos(0.5 / d) - 0.5 * sqrt(d * d - 0.25);
	return clamp(3.14159265 * d * d - 4.0 * seg, 0.0, 1.0);
}

float threshold(vec2 pix, vec2 dc)
{
	if (pattern == 1)
		return (bayer8f(dc) + 0.5) / 64.0;
	if (pattern == 2)
		return (mod(pix.y, max(1.0, linePeriod)) + 0.5) / max(1.0, linePeriod);
	if (pattern == 3) {
		vec2 p = dc + 0.5;
		vec2 u = vec2(halftoneRot.x * p.x + halftoneRot.y * p.y,
		              -halftoneRot.y * p.x + halftoneRot.x * p.y) / max(1.0, halftoneCell);
		vec2 f = fract(u) - 0.5;
		return clamp(1.0 - spotArea(length(f)), 0.001, 0.999);
	}
	return (bayer4(dc) + 0.5) / 16.0;
}

vec3 quantise(vec3 c, float l, float th)
{
	if (l <= blackPoint)
		return vec3(0.0);
	float top = max(1.0, levels - 1.0);
	if (mode == 1)
		return clamp(floor(clamp(c, 0.0, 1.0) * top + th) / top, 0.0, 1.0);
	float tq = clamp(floor(clamp(l, 0.0, 1.0) * top + th) / top, 0.0, 1.0);
	if (tq <= 0.0)
		return vec3(0.0);
	if (mode == 2) {
		if (paletteStops < 2.5)
			return mix(paletteColor[0], paletteColor[1], tq);
		return (tq < 0.5) ? mix(paletteColor[0], paletteColor[1], tq * 2.0)
		                  : mix(paletteColor[1], paletteColor[2], tq * 2.0 - 1.0);
	}
	float m = max(c.r, max(c.g, c.b));
	if (m <= 1e-5)
		return vec3(0.0);
	vec3 hue = c / m;
	vec3 o = hue * (tq / max(lum(hue), 1e-5));
	float mo = max(o.r, max(o.g, o.b));
	return (mo > 1.0) ? o / mo : o;
}

float lumAt(vec2 p)
{
	return lum(prefilter(texture2DRect(srcTex, clamp(p, vec2(0.0), srcSize - 1.0) + 0.5).rgb));
}

void main()
{
	vec2 pix = floor(coord);
	float ds = max(1.0, dotSize);
	vec2 dc = floor(pix / ds);
	vec4 d = texture2DRect(dotTex, dc + 0.5);
	vec3 col = quantise(d.rgb, d.a, threshold(pix, dc));
	if (lineMode > 0) {
		vec3 p = prefilter(texture2DRect(srcTex, pix + 0.5).rgb);
		float lp = lum(p);
		float r = max(1.0, lineReach);
		float rd = r * 0.70710678;
		vec2 o[4];
		o[0] = vec2(r, 0.0);
		o[1] = vec2(0.0, r);
		o[2] = vec2(rd, rd);
		o[3] = vec2(rd, -rd);
		float hi[4], lo[4];
		for (int i = 0; i < 4; i++) {
			float a = lumAt(pix + o[i]);
			float b = lumAt(pix - o[i]);
			hi[i] = max(a, b);
			lo[i] = min(a, b);
		}
		// Across axis i (perpendicular axis j): the step across, minus any
		// step along the line, so isolated points and textures score ~0.
		float ridge = 0.0, valley = 0.0;
		for (int i = 0; i < 4; i++) {
			int j = (i == 0) ? 1 : (i == 1) ? 0 : (i == 2) ? 3 : 2;
			ridge = max(ridge, (lp - hi[i]) - max(lp - hi[j], 0.0));
			valley = max(valley, (lo[i] - lp) - max(lo[j] - lp, 0.0));
		}
		if (ridge > lineThreshold || (lineMode == 2 && valley > lineThreshold))
			col = p;
	}
	gl_FragColor = vec4(col, 1.0);
}
