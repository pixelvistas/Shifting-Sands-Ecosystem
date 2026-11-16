// Prototype 3 (ASCII), pass 1: pass-through. Vertices are the projector
// positions of AsciiRenderer's grid lookup (scaled to character cells);
// texcoord is the data texel coordinate (rectangle texture). Shares no
// code with heightMapShader. Keep in sync with ../GL2/asciiCells.vert.
#version 150

uniform mat4 modelViewProjectionMatrix;

in vec4 position;
in vec2 texcoord;

out vec2 dataCoord;

void main()
{
	dataCoord = texcoord;
	gl_Position = modelViewProjectionMatrix * position;
}
