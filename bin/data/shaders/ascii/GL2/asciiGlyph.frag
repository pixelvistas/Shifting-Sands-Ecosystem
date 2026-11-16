// Prototype 3 (ASCII), pass 3: draws each glyph cell's character at
// projector resolution, crisp and monochrome. The grid starts at
// projector pixel (0, 0) and cells are whole pixels, matching the atlas
// cells one texel per pixel. Keep in sync with ../GL3/asciiGlyph.frag.
//
// stateTex: pass 2 (G held level, A valid). atlasTex: AsciiFontAtlas,
// printable ASCII in one row of cellSize cells, coverage in R.
// rampGlyph[level]: atlas index of the ramp character for that level.
#version 120
#extension GL_ARB_texture_rectangle : enable

uniform sampler2DRect stateTex;
uniform sampler2DRect atlasTex;
uniform vec2 cellSize;      // projector pixels (whole numbers)
uniform float rampGlyph[16];
uniform float brightness;   // 0..1

varying vec2 coord;              // projector pixel coordinate

void main()
{
	vec2 pix = floor(coord);
	// + 0.5 keeps the division clear of whole-number boundaries.
	vec2 cell = floor((pix + 0.5) / cellSize);
	vec2 local = pix - cell * cellSize;
	vec4 st = texture2DRect(stateTex, cell + 0.5);
	float lit = 0.0;
	if (st.a >= 0.5) {
		int level = int(clamp(st.g + 0.5, 0.0, 15.0));
		vec2 texel = vec2(rampGlyph[level] * cellSize.x + local.x + 0.5, local.y + 0.5);
		lit = (texture2DRect(atlasTex, texel).r >= 0.5) ? brightness : 0.0;
	}
	gl_FragColor = vec4(lit, lit, lit, 1.0);
}
