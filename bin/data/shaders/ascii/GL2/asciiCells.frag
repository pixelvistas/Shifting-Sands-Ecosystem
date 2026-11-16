// Prototype 3 (ASCII), pass 1: one output texel per character cell, the
// aggregate of a 3 x 3 footprint of Kinect cells (the footprint comes from
// the screen-space derivatives of the data coordinate). Keep in sync with
// ../GL3/asciiCells.frag.
//
// dataTex (rectangle, RGBA32F), written by AsciiRenderer::uploadData():
//   R elevation (mm); G water depth (mm, > 0), -1 snow, 0 dry;
//   B dominant role x 2 + its cover (0..1), or -1 (no plants); A 1 valid.
// Output: R mean elevation; G mean water depth when at least
// waterMinFraction of the valid samples are water, else -1 when at least
// half are snow, else 0; B the plant code of the sample with the most
// cover; A 1 (0 = no valid sample: drawn black).
#version 120
#extension GL_ARB_texture_rectangle : enable

uniform sampler2DRect dataTex;
uniform float waterMinFraction;

varying vec2 dataCoord;

void main()
{
	vec2 fx = dFdx(dataCoord) / 3.0;
	vec2 fy = dFdy(dataCoord) / 3.0;
	float n = 0.0, eSum = 0.0, wN = 0.0, wSum = 0.0, sN = 0.0;
	float bestCover = -1.0, bestCode = -1.0;
	for (int j = -1; j <= 1; j++) {
		for (int i = -1; i <= 1; i++) {
			vec4 d = texture2DRect(dataTex, dataCoord + fx * float(i) + fy * float(j));
			if (d.a < 0.5)
				continue;
			n += 1.0;
			eSum += d.r;
			if (d.g > 0.0) { wN += 1.0; wSum += d.g; }
			else if (d.g < 0.0) sN += 1.0;
			if (d.b >= 0.0) {
				float cover = d.b - 2.0 * floor(d.b * 0.5);
				if (cover > bestCover) { bestCover = cover; bestCode = d.b; }
			}
		}
	}
	if (n < 0.5) {
		gl_FragColor = vec4(0.0);
		return;
	}
	float water = 0.0;
	if (wN >= waterMinFraction * n)
		water = wSum / wN;
	else if (sN >= 0.5 * n)
		water = -1.0;
	gl_FragColor = vec4(eSum / n, water, bestCode, 1.0);
}
