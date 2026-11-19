// DitherFilter: pass-through vertex shader for both passes. texcoord is
// the pass's own texel coordinate (dot grid or source pixels). Keep in
// sync with ../GL3/dither.vert.
#version 120

varying vec2 coord;

void main()
{
	coord = gl_MultiTexCoord0.xy;
	gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
}
