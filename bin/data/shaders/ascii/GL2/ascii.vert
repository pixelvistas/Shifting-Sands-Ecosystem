// Prototype 3 (ASCII), pass 2: pass-through for the projector-sized quad;
// texcoord = projector pixel coordinate (y down). Keep in sync with
// ../GL3/ascii.vert.
#version 120

varying vec2 pixCoord;

void main()
{
	pixCoord = gl_MultiTexCoord0.xy;
	gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
}
