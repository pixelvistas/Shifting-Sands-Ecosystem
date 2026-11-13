// Prototype 2 (Mycorrhiza): pass-through vertex shader. Vertices are the
// projector positions of the module's grid lookup; texcoord is the trace
// texel coordinate (rectangle texture). Shares no code with
// heightMapShader. Keep in sync with ../GL2/mycorrhiza.vert.
#version 150

uniform mat4 modelViewProjectionMatrix;

in vec4 position;
in vec2 texcoord;

out vec2 traceCoord;

void main()
{
	traceCoord = texcoord;
	gl_Position = modelViewProjectionMatrix * position;
}
