// Prototype 2 (Mycorrhiza): hyphae. Each segment is two quads either side
// of its centre line (projector px); texcoord = (birth time, across: 1 on
// the line, 0 at the outer edge), normal.xy = grid cell position. Drawn into
// the hypha FBO with GL_MAX blending, so the joints of one thread do not
// double up. Keep in sync with ../GL2/hypha.*.
#version 150

uniform mat4 modelViewProjectionMatrix;

in vec4 position;
in vec2 texcoord;
in vec4 normal;

out vec2 birthAcross;
out vec2 gridPos;

void main()
{
	birthAcross = texcoord;
	gridPos = normal.xy;
	gl_Position = modelViewProjectionMatrix * position;
}
