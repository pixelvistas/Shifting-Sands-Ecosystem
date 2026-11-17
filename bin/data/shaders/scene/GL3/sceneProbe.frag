// Prototype 4 (Scene): a small downsampled copy of the scene's final
// luminance (sceneRender writes it to A) for the GUI histogram and the
// optional auto-exposure; read back to the CPU every few frames. Each
// probe texel point-samples the scene at its centre. Keep in sync with
// ../GL2/sceneProbe.frag.
#version 150

uniform sampler2DRect sceneTex;
uniform vec2 scale;                // scene texels per probe texel

in vec2 coord;                     // probe texel coordinate
out vec4 outputColor;

void main()
{
	outputColor = vec4(texture(sceneTex, coord * scale).a, 0.0, 0.0, 1.0);
}
