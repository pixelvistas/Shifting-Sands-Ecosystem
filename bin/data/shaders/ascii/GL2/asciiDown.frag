// AsciiFilter glow: box downsample by `factor` (2 or 4): each texel is the
// mean of a factor x factor block of the source (only texels inside it).
// Keep in sync with ../GL3/asciiDown.frag.
#version 120
#extension GL_ARB_texture_rectangle : enable

uniform sampler2DRect srcTex;
uniform vec2 srcSize;              // source texels
uniform float factor;              // 2 or 4

varying vec2 coord;                  // destination texel coordinate

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
				sum += texture2DRect(srcTex, p + 0.5).rgb;
				n += 1.0;
			}
		}
	}
	gl_FragColor = vec4(sum / max(1.0, n), 1.0);
}
