// Prototype 3 (ASCII), pass 3: draws each glyph cell at projector
// resolution: mix(bgColor[band], fgColor[band], glyph alpha) x brightness,
// with the glyph alpha 1 where the atlas coverage is >= 0.5 and 0
// elsewhere (crisp characters). The grid starts at projector pixel (0, 0)
// and cells are whole pixels, matching the atlas cells one texel per
// pixel. Keep in sync with ../GL2/asciiGlyph.frag.
//
// stateTex: pass 2 (G held glyph level, B packed band + contour index,
// A valid). atlasTex: AsciiFontAtlas, printable ASCII in one row of
// cellSize cells, coverage in R. rampGlyph[level]: atlas index of the
// ramp character for that level.
// Contour: a cell whose held contour index is above that of one of its
// four neighbours (the higher side of the crossing, one cell thick); its
// glyph colour is mixed towards white by contourBrighten. Held values
// only, so contour cells change only when glyphs could.
#version 150

uniform sampler2DRect stateTex;
uniform sampler2DRect atlasTex;
uniform vec2 cellSize;      // projector pixels (whole numbers)
uniform float rampGlyph[16];
uniform vec3 fgColor[5];    // per band, low .. high
uniform vec3 bgColor[5];
uniform int showContours;
uniform float contourBrighten;
uniform float brightness;   // 0..1

in vec2 coord;              // projector pixel coordinate
out vec4 outputColor;

const float BANDS = 5.0;

float contourIndex(vec4 st)
{
	return floor(st.b / BANDS);
}

// True when the neighbour at offset o is valid and on the lower side.
bool lowerNeighbour(vec2 cell, vec2 o, float q)
{
	vec4 n = texture(stateTex, cell + o + 0.5);
	return n.a >= 0.5 && contourIndex(n) < q;
}

void main()
{
	vec2 pix = floor(coord);
	// + 0.5 keeps the division clear of whole-number boundaries.
	vec2 cell = floor((pix + 0.5) / cellSize);
	vec2 local = pix - cell * cellSize;
	vec4 st = texture(stateTex, cell + 0.5);
	if (st.a < 0.5) {
		outputColor = vec4(0.0, 0.0, 0.0, 1.0);
		return;
	}
	float q = contourIndex(st);
	int band = int(clamp(st.b - BANDS * q + 0.5, 0.0, BANDS - 1.0));
	int level = int(clamp(st.g + 0.5, 0.0, 15.0));
	vec2 texel = vec2(rampGlyph[level] * cellSize.x + local.x + 0.5, local.y + 0.5);
	float glyphAlpha = (texture(atlasTex, texel).r >= 0.5) ? 1.0 : 0.0;

	vec3 fg = fgColor[band];
	if (showContours == 1 && (lowerNeighbour(cell, vec2(-1.0, 0.0), q) || lowerNeighbour(cell, vec2(1.0, 0.0), q)
		|| lowerNeighbour(cell, vec2(0.0, -1.0), q) || lowerNeighbour(cell, vec2(0.0, 1.0), q)))
		fg = mix(fg, vec3(1.0), contourBrighten);
	vec3 col = mix(bgColor[band], fg, glyphAlpha) * brightness;
	outputColor = vec4(col, 1.0);
}
