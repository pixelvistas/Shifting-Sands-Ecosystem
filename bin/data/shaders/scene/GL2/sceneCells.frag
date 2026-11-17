// Prototype 4 (Scene), stylise step 1 (ASCII and Both): one texel per
// glyph cell (cells of cellSize whole projector pixels, from projector
// pixel 0, 0): the mean scene colour over the cell's pixels (at most
// 32 x 32) and its perceptual luminance. Output RGB mean, A luminance
// (Rec. 709 weights). Keep in sync with ../GL3/sceneCells.frag.
#version 120
#extension GL_ARB_texture_rectangle : enable

uniform sampler2DRect warpedTex;   // projector resolution
uniform vec2 cellSize;             // projector pixels
uniform vec2 projSize;             // projector pixels

varying vec2 coord;                     // cell coordinate (cell + 0.5)

void main()
{
	vec2 origin = floor(coord) * cellSize;
	vec3 sum = vec3(0.0);
	float n = 0.0;
	for (int y = 0; y < 32; y++) {
		if (float(y) >= cellSize.y)
			break;
		for (int x = 0; x < 32; x++) {
			if (float(x) >= cellSize.x)
				break;
			vec2 p = origin + vec2(float(x), float(y));
			if (p.x < projSize.x && p.y < projSize.y) {
				sum += texture2DRect(warpedTex, p + 0.5).rgb;
				n += 1.0;
			}
		}
	}
	vec3 c = (n > 0.0) ? sum / n : vec3(0.0);
	gl_FragColor = vec4(c, dot(c, vec3(0.2126, 0.7152, 0.0722)));
}
