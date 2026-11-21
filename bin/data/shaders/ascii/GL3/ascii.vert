// Technique after "The Betta Fish in ASCII" by @dwcminh.ascii,
// https://github.com/dwcminh/ascii-art (BETTA-FISH, betta_fish.py; free for
// personal, non-commercial use). Our own GLSL; no code copied from it.
// AsciiFilter: pass-through vertex shader for every pass. texcoord is the
// pass's own texel coordinate (cell grid, glow grid or output pixels).
// Keep in sync with ../GL2/ascii.vert.
#version 150

uniform mat4 modelViewProjectionMatrix;

in vec4 position;
in vec2 texcoord;

out vec2 coord;

void main()
{
	coord = texcoord;
	gl_Position = modelViewProjectionMatrix * position;
}
