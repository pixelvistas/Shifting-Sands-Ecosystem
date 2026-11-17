// Prototype 4 (Scene), pass 2: the scene drawn through the projector
// calibration. The grid mesh (vertices at kinectCoordToProjCoord, every
// LUT_STEP cells) is drawn into a projector-sized RGBA16F FBO; coord is
// the scene texel coordinate (grid coordinate x sceneMult), sampled
// bilinearly. Keep in sync with ../GL3/sceneWarp.frag.
#version 120
#extension GL_ARB_texture_rectangle : enable

uniform sampler2DRect sceneTex;

varying vec2 coord;

void main()
{
	gl_FragColor = vec4(texture2DRect(sceneTex, coord).rgb, 1.0);
}
