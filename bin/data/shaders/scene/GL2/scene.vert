// Prototype 4 (Scene): pass-through vertex shader shared by every scene
// pass. texcoord: the pass's own texel coordinate (grid, ripple, scene or
// projector texels; see each fragment shader). Keep in sync with
// ../GL3/scene.vert.
#version 120

varying vec2 coord;

void main()
{
	coord = gl_MultiTexCoord0.xy;
	gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
}
