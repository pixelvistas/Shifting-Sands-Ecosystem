// Prototype 3 (ASCII), pass 2: pass-through for the projector-sized quad;
// texcoord = projector pixel coordinate (y down). Keep in sync with
// ../GL2/ascii.vert.
#version 150

uniform mat4 modelViewProjectionMatrix;

in vec4 position;
in vec2 texcoord;

out vec2 pixCoord;

void main()
{
	pixCoord = texcoord;
	gl_Position = modelViewProjectionMatrix * position;
}
