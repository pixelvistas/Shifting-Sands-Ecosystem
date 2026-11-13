// Prototype 2 (Mycorrhiza): hyphae. Each segment is two quads either side
// of its centre line (projector px); texcoord = (birth time, across: 1 on
// the line, 0 at the outer edge), normal.xy = grid cell position. Drawn into
// the hypha FBO with GL_MAX blending, so the joints of one thread do not
// double up. Keep in sync with ../GL3/hypha.*.
#version 120

varying vec2 birthAcross;
varying vec2 gridPos;

void main()
{
	birthAcross = gl_MultiTexCoord0.xy;
	gridPos = gl_Normal.xy;
	gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
}
