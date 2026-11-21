// Technique after "The Betta Fish in ASCII" by @dwcminh.ascii,
// https://github.com/dwcminh/ascii-art (BETTA-FISH, betta_fish.py; free for
// personal, non-commercial use). Our own GLSL; no code copied from it.
// AsciiFilter pass 8: trails. Output = max(previous output x amount, this
// frame), per channel (A, the glow mask, too). Values under 1/1024 are set
// to 0 so a trail always fades to exact black.
// Keep in sync with ../GL3/asciiTrail.frag.
#version 120
#extension GL_ARB_texture_rectangle : enable

uniform sampler2DRect curTex;      // this frame (pass 7)
uniform sampler2DRect prevTex;     // last frame's output of this pass
uniform float amount;              // 0..1

varying vec2 coord;                  // output pixel coordinate

void main()
{
	vec4 o = max(texture2DRect(curTex, coord), texture2DRect(prevTex, coord) * amount);
	if (max(o.r, max(o.g, o.b)) < 1.0 / 1024.0)
		o.rgb = vec3(0.0);
	if (o.a < 1.0 / 1024.0)
		o.a = 0.0;
	gl_FragColor = o;
}
