// Prototype 3 (ASCII): pass-through vertex shader shared by the three
// passes (asciiSample, asciiState, asciiGlyph). texcoord: field texels
// (asciiSample), cell coordinates (asciiState) or projector pixels
// (asciiGlyph). Shares no code with heightMapShader. Keep in sync with
// ../GL3/ascii.vert.
#version 120

varying vec2 coord;

void main()
{
	coord = gl_MultiTexCoord0.xy;
	gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
}
