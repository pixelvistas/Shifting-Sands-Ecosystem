// DitherFilter pass 1: one texel per dot (dotSize x dotSize source
// pixels, from the top-left of the source). RGB = mean of the dot's
// pre-filtered pixels, A = luminance of that mean. Partial dots at the
// right and bottom edges average only the pixels inside the source.
// Pre-filter (shared with ditherOut.frag, keep identical): exposure
// (multiplies the colour), contrast (on luminance, about 0.5),
// saturation (away from the pixel's grey, luminance unchanged); hue is
// kept, and a colour brighter than 1 in any channel is divided by its
// maximum channel instead of being clipped per channel.
// Keep in sync with ../GL2/ditherDots.frag.
#version 150

uniform sampler2DRect srcTex;      // source, in pixels
uniform vec2 srcSize;              // pixels
uniform float dotSize;             // pixels, 1..8
uniform float exposure;
uniform float contrast;
uniform float saturation;

in vec2 coord;                     // dot-grid coordinate
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
	float ls = lum(c);
	float lt = max((l - 0.5) * contrast + 0.5, 0.0);
	c = (ls > 1e-5) ? c * (lt / ls) : vec3(0.0);
	float m = max(c.r, max(c.g, c.b));
	return (m > 1.0) ? c / m : c;
}

void main()
{
	float ds = max(1.0, dotSize);
	vec2 origin = floor(coord) * ds;
	vec3 sum = vec3(0.0);
	float n = 0.0;
	for (int y = 0; y < 8; y++) {
		if (float(y) >= ds)
			break;
		for (int x = 0; x < 8; x++) {
			if (float(x) >= ds)
				break;
			vec2 p = origin + vec2(float(x), float(y));
			if (p.x < srcSize.x && p.y < srcSize.y) {
				sum += prefilter(texture(srcTex, p + 0.5).rgb);
				n += 1.0;
			}
		}
	}
	vec3 c = sum / max(1.0, n);
	outputColor = vec4(c, lum(c));
}
