// Prototype 4 (Scene), pass 2: the scene drawn through the projector
// calibration. The grid mesh (vertices at kinectCoordToProjCoord, every
// LUT_STEP cells) is drawn into a projector-sized RGBA16F FBO; coord is
// the scene texel coordinate (grid coordinate x sceneMult), sampled
// bilinearly. Keep in sync with ../GL2/sceneWarp.frag.
#version 150

uniform sampler2DRect sceneTex;

in vec2 coord;
out vec4 outputColor;

void main()
{
	outputColor = vec4(texture(sceneTex, coord).rgb, 1.0);
}
