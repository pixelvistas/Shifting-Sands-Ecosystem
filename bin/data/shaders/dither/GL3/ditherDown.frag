// DitherFilter: one level of the dot pyramid. Each texel = mean of the
// (up to) 2 x 2 texels of the level below that lie inside it (RGB mean
// colour, A its luminance: both linear, so averaging them is exact), so
// level k holds dots of dotSize x 2^k pixels on a grid aligned with every
// finer level. Keep in sync with ../GL2/ditherDown.frag.
#version 150

uniform sampler2DRect lowerTex;    // the level below
uniform vec2 lowerSize;            // its texels

in vec2 coord;                     // this level's texel coordinate
out vec4 outputColor;

void main()
{
	vec2 base = floor(coord) * 2.0;
	vec4 sum = vec4(0.0);
	float n = 0.0;
	for (int y = 0; y < 2; y++) {
		for (int x = 0; x < 2; x++) {
			vec2 p = base + vec2(float(x), float(y));
			if (p.x < lowerSize.x && p.y < lowerSize.y) {
				sum += texture(lowerTex, p + 0.5);
				n += 1.0;
			}
		}
	}
	outputColor = sum / max(1.0, n);
}
