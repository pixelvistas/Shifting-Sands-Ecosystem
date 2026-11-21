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
