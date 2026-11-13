// Prototype 2 (Mycorrhiza): hyphae. Each segment is two quads either side
// of its centre line (projector px); texcoord = (birth time, across: 1 on
// the line, 0 at the outer edge), normal.xy = grid cell position. Drawn into
// the hypha FBO with GL_MAX blending, so the joints of one thread do not
// double up. Keep in sync with ../GL3/hypha.*.
#version 120
#extension GL_ARB_texture_rectangle : enable

uniform sampler2DRect warmTex; // per touch block: min(1, gain x blurred presence x maturity)
uniform float time;         // field time, s
uniform float memSec;       // trace memory: fade to 10 % over this
uniform float blockSize;    // grid cells per touch block
uniform float brightness;
uniform vec3 pale;          // #E6F7FF
uniform vec3 warm;          // #FFE2B8, near mature patches

varying vec2 birthAcross;
varying vec2 gridPos;

void main()
{
	float fade = pow(0.1, max(0.0, time - birthAcross.x) / memSec);
	float w = texture2DRect(warmTex, gridPos / blockSize).r;
	vec3 col = mix(pale, warm, w) * fade * birthAcross.y * brightness;
	gl_FragColor = vec4(col, 1.0);
}
