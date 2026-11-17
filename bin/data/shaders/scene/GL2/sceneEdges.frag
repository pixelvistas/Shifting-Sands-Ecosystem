// Prototype 4 (Scene), stylise step 1b (ASCII and Both): per glyph cell,
// the structure tensor of the luminance gradient summed over the cell's
// pixels (central differences on the warped scene, projector pixels, so
// directions are screen directions after the warp). Keep in sync with
// ../GL3/sceneEdges.frag; tools/scene_preview runs this file.
//
// J = mean over the cell of [gx^2, gy^2, gx gy] (y down). Its eigenvalues
// l1 >= l2; edge strength E = sqrt(l1 - l2): the coherent gradient
// magnitude (luminance per pixel), large for a stroke or edge with one
// orientation, small for flat cells and for isotropic texture. A thin
// bright blade on black has gradients of both signs on its two sides;
// they cancel in a plain sum but not in J (quadratic), which is why the
// tensor is used rather than a mean gradient. Dominant gradient angle
// phi = 0.5 atan2(2 Jxy, Jxx - Jyy); the stroke runs perpendicular:
// strokeAngle = phi + 90 degrees, folded to 0..180 (0 = horizontal,
// 45 = down-right "\\", 90 = vertical, 135 = up-right "/", y down).
// Output: R = E, G = stroke angle (degrees), A = 1.
#version 120
#extension GL_ARB_texture_rectangle : enable

uniform sampler2DRect warpedTex;   // projector resolution
uniform vec2 cellSize;             // projector pixels
uniform vec2 projSize;

varying vec2 coord;                     // cell coordinate (cell + 0.5)

float lumAt(vec2 p)
{
	return dot(texture2DRect(warpedTex, clamp(p, vec2(0.5), projSize - 0.5)).rgb, vec3(0.2126, 0.7152, 0.0722));
}

void main()
{
	vec2 origin = floor(coord) * cellSize;
	float jxx = 0.0, jyy = 0.0, jxy = 0.0, n = 0.0;
	for (int y = 0; y < 32; y++) {
		if (float(y) >= cellSize.y)
			break;
		for (int x = 0; x < 32; x++) {
			if (float(x) >= cellSize.x)
				break;
			vec2 p = origin + vec2(float(x), float(y)) + 0.5;
			float gx = 0.5 * (lumAt(p + vec2(1.0, 0.0)) - lumAt(p - vec2(1.0, 0.0)));
			float gy = 0.5 * (lumAt(p + vec2(0.0, 1.0)) - lumAt(p - vec2(0.0, 1.0)));
			jxx += gx * gx;
			jyy += gy * gy;
			jxy += gx * gy;
			n += 1.0;
		}
	}
	n = max(n, 1.0);
	jxx /= n;
	jyy /= n;
	jxy /= n;
	float d = sqrt((jxx - jyy) * (jxx - jyy) + 4.0 * jxy * jxy);
	float e = sqrt(max(d, 0.0)); // l1 - l2 = d
	float phi = 0.5 * atan(2.0 * jxy, jxx - jyy);
	float stroke = mod(degrees(phi) + 90.0, 180.0);
	gl_FragColor = vec4(e, stroke, 0.0, 1.0);
}
