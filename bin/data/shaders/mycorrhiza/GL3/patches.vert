// Prototype 2 (Mycorrhiza): pass-through for patches.frag; texcoord = grid cell
// coordinate (rectangle texture). Keep in sync with ../GL2/patches.vert.
#version 150

uniform mat4 modelViewProjectionMatrix;

in vec4 position;
in vec2 texcoord;

out vec2 cellCoord;

void main()
{
	cellCoord = texcoord;
	gl_Position = modelViewProjectionMatrix * position;
}
