// Prototype 2 (Mycorrhiza): pass-through for patches.frag; texcoord = grid cell
// coordinate (rectangle texture). Keep in sync with ../GL3/patches.vert.
#version 120

varying vec2 cellCoord;

void main()
{
	cellCoord = gl_MultiTexCoord0.xy;
	gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
}
