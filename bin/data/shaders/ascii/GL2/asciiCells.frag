// AsciiFilter pass 1: one texel per character cell (cellSize output
// pixels, grid from the top-left). The colour source is sampled at a
// fixed 4 x 4 grid of points inside the cell (sample centres at
// (i + 0.5) / 4 of the cell). A sample is covered when its brightest
// channel exceeds blackThreshold. Output: RGB = mean of the covered
// samples after the pre-filter (exposure, then saturation about the
// sample's luminance; hue kept; a colour brighter than 1 is divided by
// its maximum channel), A = coverage (covered / samples inside the
// source). Partial cells at the right and bottom count only samples
// inside the source.
// Keep in sync with ../GL3/asciiCells.frag.
#version 120
#extension GL_ARB_texture_rectangle : enable

uniform sampler2DRect srcTex;      // colour source, in pixels
uniform vec2 srcSize;              // pixels
uniform vec2 cellSize;             // pixels (w, h)
uniform float blackThreshold;      // max channel
uniform float exposure;
uniform float saturation;

varying vec2 coord;                  // cell-grid coordinate

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

void main()
{
	vec2 origin = floor(coord) * cellSize;
	vec3 sum = vec3(0.0);
	float n = 0.0, k = 0.0;
	for (int y = 0; y < 4; y++) {
		for (int x = 0; x < 4; x++) {
			vec2 p = floor(origin + (vec2(float(x), float(y)) + 0.5) * 0.25 * cellSize);
			if (p.x < srcSize.x && p.y < srcSize.y) {
				vec3 c = texture2DRect(srcTex, p + 0.5).rgb;
				n += 1.0;
				if (max(c.r, max(c.g, c.b)) > blackThreshold) {
					sum += prefilter(c);
					k += 1.0;
				}
			}
		}
	}
	gl_FragColor = vec4((k > 0.0) ? sum / k : vec3(0.0), k / max(1.0, n));
}
