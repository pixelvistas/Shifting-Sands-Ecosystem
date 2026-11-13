// Prototype 2 (Mycorrhiza): pass-through vertex shader. Vertices are the
// projector positions of the module's grid lookup; the texture coordinate
// is the trace texel coordinate (rectangle texture). Shares no code with
// heightMapShader. Keep in sync with ../GL3/mycorrhiza.vert.
#version 120

varying vec2 traceCoord;

void main()
{
	traceCoord = gl_MultiTexCoord0.xy;
	gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
}
