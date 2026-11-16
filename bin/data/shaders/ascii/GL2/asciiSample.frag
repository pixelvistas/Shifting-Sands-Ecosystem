// Prototype 3 (ASCII), pass 1: one output texel per glyph cell. The
// field mesh is drawn scaled to the cell grid, so each fragment is one
// cell centre; it averages a 3 x 3 footprint of the field spread over the
// cell (footprint from the derivatives of the field coordinate). Keep in
// sync with ../GL3/asciiSample.frag.
//
// fieldTex (rectangle, R32F): value 0..1; negative = no data.
// Output: R mean of the valid taps, A 1 (A 0: no valid tap).
#version 120
#extension GL_ARB_texture_rectangle : enable

uniform sampler2DRect fieldTex;

varying vec2 coord;

void main()
{
	vec2 fx = dFdx(coord) / 3.0;
	vec2 fy = dFdy(coord) / 3.0;
	float n = 0.0;
	float sum = 0.0;
	for (int j = -1; j <= 1; j++) {
		for (int i = -1; i <= 1; i++) {
			float v = texture2DRect(fieldTex, coord + fx * float(i) + fy * float(j)).r;
			if (v >= 0.0) {
				n += 1.0;
				sum += v;
			}
		}
	}
	gl_FragColor = (n > 0.5) ? vec4(sum / n, 0.0, 0.0, 1.0) : vec4(0.0);
}
