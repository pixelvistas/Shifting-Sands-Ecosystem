// Prototype 4 (Scene): a small downsampled copy of the scene's final
// luminance (sceneRender writes it to A) for the GUI histogram and the
// optional auto-exposure; read back to the CPU every few frames. Each
// probe texel point-samples the scene at its centre. Keep in sync with
// ../GL3/sceneProbe.frag.
#version 120
#extension GL_ARB_texture_rectangle : enable

uniform sampler2DRect sceneTex;
uniform vec2 scale;                // scene texels per probe texel

varying vec2 coord;                     // probe texel coordinate

void main()
{
	gl_FragColor = vec4(texture2DRect(sceneTex, coord * scale).a, 0.0, 0.0, 1.0);
}
