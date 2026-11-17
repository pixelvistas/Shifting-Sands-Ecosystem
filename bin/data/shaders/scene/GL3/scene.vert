// Prototype 4 (Scene): pass-through vertex shader shared by every scene
// pass. texcoord: the pass's own texel coordinate (grid, ripple, scene or
// projector texels; see each fragment shader). Keep in sync with
// ../GL2/scene.vert.
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
