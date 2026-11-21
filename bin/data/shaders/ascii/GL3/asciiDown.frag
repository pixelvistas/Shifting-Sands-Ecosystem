// Technique after "The Betta Fish in ASCII" by @dwcminh.ascii,
// https://github.com/dwcminh/ascii-art (BETTA-FISH, betta_fish.py; free for
// personal, non-commercial use). Our own GLSL; no code copied from it.
// AsciiFilter glow: box downsample by `factor` (2 or 4): each texel is the
// mean of a factor x factor block of the source (only texels inside it).
// Keep in sync with ../GL2/asciiDown.frag.
#version 150

uniform sampler2DRect srcTex;
uniform vec2 srcSize;              // source texels
uniform float factor;              // 2 or 4

in vec2 coord;                     // destination texel coordinate
out vec4 outputColor;

void main()
{
	vec2 origin = floor(coord) * factor;
	vec3 sum = vec3(0.0);
	float n = 0.0;
	for (int y = 0; y < 4; y++) {
		if (float(y) >= factor)
			break;
		for (int x = 0; x < 4; x++) {
			if (float(x) >= factor)
				break;
			vec2 p = origin + vec2(float(x), float(y));
			if (p.x < srcSize.x && p.y < srcSize.y) {
				sum += texture(srcTex, p + 0.5).rgb;
				n += 1.0;
			}
		}
	}
	outputColor = vec4(sum / max(1.0, n), 1.0);
}
