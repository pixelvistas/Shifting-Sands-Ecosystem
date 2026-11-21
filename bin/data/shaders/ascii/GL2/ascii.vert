// Technique after "The Betta Fish in ASCII" by @dwcminh.ascii,
// https://github.com/dwcminh/ascii-art (BETTA-FISH, betta_fish.py; free for
// personal, non-commercial use). Our own GLSL; no code copied from it.
// AsciiFilter: pass-through vertex shader for every pass. texcoord is the
// pass's own texel coordinate (cell grid, glow grid or output pixels).
// Keep in sync with ../GL3/ascii.vert.
#version 120

varying vec2 coord;

void main()
{
	coord = gl_MultiTexCoord0.xy;
	gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
}
