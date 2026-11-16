// Prototype 3 (ASCII): pass-through vertex shader shared by the three
// passes (asciiSample, asciiState, asciiGlyph). texcoord: field texels
// (asciiSample), cell coordinates (asciiState) or projector pixels
// (asciiGlyph). Shares no code with heightMapShader. Keep in sync with
// ../GL2/ascii.vert.
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
