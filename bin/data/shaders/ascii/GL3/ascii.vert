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
