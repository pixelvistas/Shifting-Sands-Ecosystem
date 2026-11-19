// DitherFilter: pass-through vertex shader for both passes. texcoord is
// the pass's own texel coordinate (dot grid or source pixels). Keep in
// sync with ../GL2/dither.vert.
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
