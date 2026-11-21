// Technique after "The Betta Fish in ASCII" by @dwcminh.ascii,
// https://github.com/dwcminh/ascii-art (BETTA-FISH, betta_fish.py; free for
// personal, non-commercial use). Our own GLSL; no code copied from it.
// AsciiFilter pass 2 (after asciiParts): one texel per character cell
// (cellSize output pixels, grid from the top-left). Every pixel of the cell
// is read. A pixel is covered when its brightest channel exceeds
// blackThreshold. Output:
//   A = coverage = covered pixels / pixels inside the source (the image
//       counterpart of the reference's summed point area per cell);
//   RGB = mean of the covered pixels that belong to the cell's own part
//       (pass 1), after the pre-filter (exposure, then saturation about the
//       pixel's luminance; hue kept; a colour brighter than 1 is divided by
//       its maximum channel). The reference takes the colour of one point,
//       the nearest; one part's mean is its 2D counterpart (no mixing across
//       a boundary). If no covered pixel has the cell's part, the mean of all
//       covered pixels.
// Cells are at most 16 x 16 pixels.
// Keep in sync with ../GL2/asciiCells.frag.
#version 150

uniform sampler2DRect srcTex;      // colour source, in pixels
uniform sampler2DRect partsTex;    // parts source, in its own pixels
uniform sampler2DRect cellPartTex; // pass 1: the part of each cell
uniform vec2 srcSize;              // pixels
uniform vec2 partsScale;           // parts pixels per colour pixel
uniform vec2 cellSize;             // pixels (w, h), <= 16
uniform float blackThreshold;      // max channel
uniform float exposure;
uniform float saturation;

in vec2 coord;                     // cell-grid coordinate
out vec4 outputColor;

float lum(vec3 c)
{
	return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

vec3 prefilter(vec3 c)
{
	c = max(c, vec3(0.0)) * exposure;
	float l = lum(c);
	c = max(vec3(l) + (c - vec3(l)) * saturation, vec3(0.0));
	float m = max(c.r, max(c.g, c.b));
	return (m > 1.0) ? c / m : c;
}

// One number per part value (exact in a float: < 2^24); -1 = no part.
float partKey(vec4 p)
{
	vec4 q = floor(p * 255.0 + 0.5);
	return (q.a < 0.5) ? -1.0 : q.r * 65536.0 + q.g * 256.0 + q.b;
}

void main()
{
	vec2 cell = floor(coord);
	vec2 origin = cell * cellSize;
	float own = partKey(texture(cellPartTex, cell + 0.5));
	vec3 sumAll = vec3(0.0), sumPart = vec3(0.0);
	float n = 0.0, k = 0.0, kp = 0.0;
	for (int y = 0; y < 16; y++) {
		if (float(y) >= cellSize.y)
			break;
		for (int x = 0; x < 16; x++) {
			if (float(x) >= cellSize.x)
				break;
			vec2 p = origin + vec2(float(x), float(y));
			if (p.x < srcSize.x && p.y < srcSize.y) {
				n += 1.0;
				vec3 c = texture(srcTex, p + 0.5).rgb;
				if (max(c.r, max(c.g, c.b)) > blackThreshold) {
					vec3 f = prefilter(c);
					sumAll += f;
					k += 1.0;
					if (partKey(texture(partsTex, (p + 0.5) * partsScale)) == own) {
						sumPart += f;
						kp += 1.0;
					}
				}
			}
		}
	}
	vec3 colour = (kp > 0.0) ? sumPart / kp : ((k > 0.0) ? sumAll / k : vec3(0.0));
	outputColor = vec4(colour, k / max(1.0, n));
}
