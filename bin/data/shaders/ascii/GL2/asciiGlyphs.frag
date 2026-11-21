// AsciiFilter pass 5: per output pixel, the cell's character (pass 4) from
// the font atlas (ASCII 32..126, 16 per row, one cellSize slot each; R =
// glyph coverage), times the cell's colour. Output A = 1 in every pixel of
// a drawn cell, 0 in empty cells (the glow mask).
// Keep in sync with ../GL3/asciiGlyphs.frag.
#version 120
#extension GL_ARB_texture_rectangle : enable

uniform sampler2DRect decideTex;   // pass 4, one texel per cell
uniform sampler2DRect atlasTex;    // font atlas, in pixels
uniform vec2 cellSize;             // pixels (w, h)

varying vec2 coord;                  // output pixel coordinate

void main()
{
	vec2 p = floor(coord);
	vec2 cell = floor((p + 0.5) / cellSize);
	vec2 local = p - cell * cellSize;
	vec4 d = texture2DRect(decideTex, cell + 0.5);
	float code = floor(d.a + 0.5);
	if (code < 32.5 || code > 126.5) {
		gl_FragColor = vec4(0.0);
		return;
	}
	float slot = code - 32.0;
	vec2 slotOrigin = vec2(mod(slot, 16.0), floor((slot + 0.5) / 16.0)) * cellSize;
	float a = texture2DRect(atlasTex, slotOrigin + local + 0.5).r;
	gl_FragColor = vec4(d.rgb * a, 1.0);
}
