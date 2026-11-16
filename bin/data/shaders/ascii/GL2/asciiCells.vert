// Prototype 3 (ASCII), pass 1: pass-through. Vertices are the projector
// positions of AsciiRenderer's grid lookup (scaled to character cells);
// texcoord is the data texel coordinate (rectangle texture). Shares no
// code with heightMapShader. Keep in sync with ../GL3/asciiCells.vert.
#version 120

varying vec2 dataCoord;

void main()
{
	dataCoord = gl_MultiTexCoord0.xy;
	gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
}
