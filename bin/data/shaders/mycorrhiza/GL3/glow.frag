// Prototype 2 (Mycorrhiza): composites the hypha FBO with one glow pass:
// the hypha image plus strength x a ring blur (8 taps at radius, 8 at
// 2 x radius, weight 0.5). Samples by gl_FragCoord, so the source FBO must
// be the same size as the target. Keep in sync with ../GL2/glow.*.
#version 150

uniform sampler2DRect src;  // the hypha FBO
uniform float radius;       // px
uniform float strength;

out vec4 outputColor;

void main()
{
	vec2 c = gl_FragCoord.xy;
	vec3 g = vec3(0.0);
	for (int d = 0; d < 8; d++) {
		float a = 0.7853982 * float(d);
		vec2 dir = vec2(cos(a), sin(a));
		g += texture(src, c + dir * radius).rgb;
		g += 0.5 * texture(src, c + dir * 2.0 * radius).rgb;
	}
	outputColor = vec4(texture(src, c).rgb + strength * g / 12.0, 1.0);
}
