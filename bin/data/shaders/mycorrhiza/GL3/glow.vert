// Prototype 2 (Mycorrhiza): composites the hypha FBO with one glow pass:
// the hypha image plus strength x a ring blur (8 taps at radius, 8 at
// 2 x radius, weight 0.5). Samples by gl_FragCoord, so the source FBO must
// be the same size as the target. Keep in sync with ../GL2/glow.*.
#version 150

uniform mat4 modelViewProjectionMatrix;

in vec4 position;

void main()
{
	gl_Position = modelViewProjectionMatrix * position;
}
