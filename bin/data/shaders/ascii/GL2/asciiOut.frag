// Technique after "The Betta Fish in ASCII" by @dwcminh.ascii,
// https://github.com/dwcminh/ascii-art (BETTA-FISH, betta_fish.py; free for
// personal, non-commercial use). Our own GLSL; no code copied from it.
// AsciiFilter pass 9: output = frame (trail result) + tightAmount x tight
// glow + wideAmount x wide glow, clamped to 1. Each glow is first put
// through the black floor: x smoothstep(blackFloor, 2 blackFloor, its
// brightest channel), so glow at or below the floor is exactly 0. Unless
// glowOutside is 1, the glow is also multiplied by the frame's A (1 in
// drawn cells, fading with the trail, 0 in empty cells), so empty cells
// stay exact black (the reference has neither floor nor mask: its glow
// also lights the water around the fish; on the sand that would light bare
// sand, so the mask stays on by default and the floor defaults to 0). The
// glow textures are sampled with bilinear filtering (the reference scales
// its glow layers back up with smoothscale).
// Keep in sync with ../GL3/asciiOut.frag.
#version 120
#extension GL_ARB_texture_rectangle : enable

uniform sampler2DRect frameTex;    // full resolution
uniform sampler2DRect tightTex;    // 1/4 resolution, linear filtering
uniform sampler2DRect wideTex;     // 1/8 resolution, linear filtering
uniform int glowOn;
uniform float tightAmount;
uniform float wideAmount;
uniform float blackFloor;
uniform int glowOutside;

varying vec2 coord;                  // output pixel coordinate

vec3 floored(vec3 c)
{
	float m = max(c.r, max(c.g, c.b));
	return (blackFloor > 0.0) ? c * smoothstep(blackFloor, 2.0 * blackFloor, m) : c;
}

void main()
{
	vec4 f = texture2DRect(frameTex, coord);
	vec3 c = f.rgb;
	if (glowOn == 1) {
		vec3 glow = tightAmount * floored(texture2DRect(tightTex, coord * 0.25).rgb)
			+ wideAmount * floored(texture2DRect(wideTex, coord * 0.125).rgb);
		c += glow * ((glowOutside == 1) ? 1.0 : f.a);
	}
	gl_FragColor = vec4(min(c, vec3(1.0)), 1.0);
}
