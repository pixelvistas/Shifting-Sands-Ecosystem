// Prototype 3 (ASCII), pass 2: one glyph per character cell, upright in
// projector space, on black. Shares no code with heightMapShader. Keep in
// sync with ../GL2/ascii.frag.
//
// cellTex (rectangle, RGBA32F, one texel per cell): see asciiCells.frag.
// glyphTex (rectangle): AsciiFont's atlas, glyphPx square glyphs in one
// row; the *Glyph uniforms are atlas indices set by AsciiRenderer.
// Priority per cell: snow, water, contour (or plant first when
// contoursOverPlants is 0), plant, land density ramp.
#version 150

uniform sampler2DRect cellTex;
uniform sampler2DRect glyphTex;
uniform float cellPx;
uniform vec2 cellGrid;
uniform float glyphPx;
uniform float landGlyph[8];
uniform float plantGlyph[9];   // role x 3 + level
uniform float waterGlyph[4];
uniform float contourGlyph[4]; // contour tangent 0, 45, 90, 135 deg (y down)
uniform float snowGlyph;
uniform float landMinMM;
uniform float landMaxMM;
uniform int showContours;
uniform float contourIntervalMM;
uniform float contourOffsetMM;  // contours at offset + k x interval
uniform int contoursOverPlants;
uniform float plantMinCover;
uniform float waterFullMM;
uniform float wavePhase;
uniform float waveLengthCells;
uniform float landDim;
uniform float brightness;
uniform vec3 landColor;
uniform vec3 contourColor;
uniform vec3 waterColor;
uniform vec3 plantColor[3];
uniform vec3 snowColor;

in vec2 pixCoord;
out vec4 outputColor;

vec4 cellAt(vec2 c)
{
	if (c.x < 0.0 || c.y < 0.0 || c.x >= cellGrid.x || c.y >= cellGrid.y)
		return vec4(0.0);
	return texture(cellTex, c + 0.5);
}

// Elevation of a neighbour; the centre's own when the neighbour is
// outside the sand, under water or snow, so no contour is drawn there.
float landElevation(vec4 nb, float self)
{
	return (nb.a < 0.5 || nb.g != 0.0) ? self : nb.r;
}

void main()
{
	vec2 cell = floor(pixCoord / cellPx);
	vec2 local = (pixCoord - cell * cellPx) / cellPx;
	vec4 d = cellAt(cell);
	if (d.a < 0.5) {
		outputColor = vec4(0.0, 0.0, 0.0, 1.0);
		return;
	}

	float glyph = landGlyph[0];
	vec3 col = vec3(0.0);
	if (d.g < 0.0) {
		glyph = snowGlyph;
		col = snowColor;
	} else if (d.g > 0.0) {
		float w = clamp(d.g / waterFullMM, 0.0, 1.0);
		float phase = fract((cell.x + 0.5 * cell.y) / waveLengthCells - wavePhase);
		glyph = waterGlyph[int(min(3.0, floor(phase * 4.0)))];
		col = waterColor * (0.35 + 0.65 * w);
	} else {
		float eL = landElevation(cellAt(cell + vec2(-1.0, 0.0)), d.r);
		float eR = landElevation(cellAt(cell + vec2(1.0, 0.0)), d.r);
		float eU = landElevation(cellAt(cell + vec2(0.0, -1.0)), d.r);
		float eD = landElevation(cellAt(cell + vec2(0.0, 1.0)), d.r);
		// Drawn on the higher side of each crossing (one cell thick).
		float level = floor((d.r - contourOffsetMM) / contourIntervalMM);
		bool contour = showContours == 1 && (floor((eL - contourOffsetMM) / contourIntervalMM) < level
			|| floor((eR - contourOffsetMM) / contourIntervalMM) < level
			|| floor((eU - contourOffsetMM) / contourIntervalMM) < level
			|| floor((eD - contourOffsetMM) / contourIntervalMM) < level);
		bool plant = d.b >= 0.0;
		float role = plant ? floor(d.b * 0.5) : 0.0;
		float cover = plant ? d.b - 2.0 * role : 0.0;
		if (contour && (contoursOverPlants == 1 || !plant)) {
			// Gradient (y down) -> contour tangent, folded to 0..180 deg.
			vec2 g = vec2(eR - eL, eD - eU);
			vec2 t = vec2(-g.y, g.x);
			float a = (dot(t, t) > 0.0) ? atan(t.y, t.x) : 0.0;
			a = mod(a + 3.14159265, 3.14159265);
			float k = mod(floor(a / 0.78539816 + 0.5), 4.0);
			glyph = contourGlyph[int(k)];
			col = contourColor;
		} else if (plant) {
			float level3 = min(2.0, floor((cover - plantMinCover) / max(1e-3, 1.0 - plantMinCover) * 3.0));
			int r = int(clamp(role, 0.0, 2.0));
			glyph = plantGlyph[r * 3 + int(max(0.0, level3))];
			col = plantColor[r] * (0.6 + 0.4 * cover);
		} else {
			float t = clamp((d.r - landMinMM) / (landMaxMM - landMinMM), 0.0, 1.0);
			glyph = landGlyph[int(min(7.0, floor(t * 8.0)))];
			col = landColor * mix(landDim, 1.0, t);
		}
	}

	vec2 texel = vec2(glyph * glyphPx + floor(local.x * glyphPx) + 0.5, floor(local.y * glyphPx) + 0.5);
	float v = texture(glyphTex, texel).r;
	outputColor = vec4(min(col * v * brightness, vec3(1.0)), 1.0);
}
