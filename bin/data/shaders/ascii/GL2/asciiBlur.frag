// AsciiFilter glow: one direction of a separable Gaussian, 9 taps at
// texel centres (-4..4 x dir), sigma in texels, weights normalised. The
// edge texel repeats beyond the texture (clamp to edge).
// Keep in sync with ../GL3/asciiBlur.frag.
#version 120
#extension GL_ARB_texture_rectangle : enable

uniform sampler2DRect srcTex;
uniform vec2 dir;                  // (1, 0) or (0, 1)
uniform float sigma;               // texels

varying vec2 coord;                  // texel coordinate

void main()
{
	vec2 p = floor(coord) + 0.5;
	vec3 sum = vec3(0.0);
	float wsum = 0.0;
	for (int i = -4; i <= 4; i++) {
		float t = float(i);
		float w = exp(-0.5 * t * t / (sigma * sigma));
		sum += texture2DRect(srcTex, p + dir * t).rgb * w;
		wsum += w;
	}
	gl_FragColor = vec4(sum / wsum, 1.0);
}
