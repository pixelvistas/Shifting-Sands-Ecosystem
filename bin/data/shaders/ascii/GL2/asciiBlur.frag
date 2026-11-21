// Technique after "The Betta Fish in ASCII" by @dwcminh.ascii,
// https://github.com/dwcminh/ascii-art (BETTA-FISH, betta_fish.py; free for
// personal, non-commercial use). Our own GLSL; no code copied from it.
// AsciiFilter glow: one direction of a separable Gaussian at texel centres,
// taps -6..6 x dir, sigma in texels, weights normalised; taps further than
// `reach` texels are left out. The edge texel repeats beyond the texture
// (clamp to edge). AsciiFilter uses sigma 1.78 / reach 4 (tight) and 2.40 /
// reach 5 (wide): pygame-ce's gaussian_blur at radius 2 and 3, as the
// reference uses, measured on an impulse.
// Keep in sync with ../GL3/asciiBlur.frag.
#version 120
#extension GL_ARB_texture_rectangle : enable

uniform sampler2DRect srcTex;
uniform vec2 dir;                  // (1, 0) or (0, 1)
uniform float sigma;               // texels
uniform float reach;               // texels, <= 6

varying vec2 coord;                  // texel coordinate

void main()
{
	vec2 p = floor(coord) + 0.5;
	vec3 sum = vec3(0.0);
	float wsum = 0.0;
	for (int i = -6; i <= 6; i++) {
		float t = float(i);
		if (abs(t) > reach)
			continue;
		float w = exp(-0.5 * t * t / (sigma * sigma));
		sum += texture2DRect(srcTex, p + dir * t).rgb * w;
		wsum += w;
	}
	gl_FragColor = vec4(sum / wsum, 1.0);
}
