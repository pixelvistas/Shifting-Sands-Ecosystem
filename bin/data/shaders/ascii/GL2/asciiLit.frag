// Technique after "The Betta Fish in ASCII" by @dwcminh.ascii,
// https://github.com/dwcminh/ascii-art (BETTA-FISH, betta_fish.py; free for
// personal, non-commercial use). Our own GLSL; no code copied from it.
// AsciiFilter pass 3 (2026-11-21): the lit brightness of each cell. The
// reference lights its 3D fish (a key light, a specular term through the
// half vector between light and view, and a rim term where the surface
// turns away from the viewer); here the same terms light the surface
// normals the caller supplies (Sandbox: the sand, seen from straight above,
// so the view vector is (0, 0, 1)). Per pixel of the cell that belongs to
// the cell's part (pass 1), averaged:
//   ground and life (parts G 0, 1):
//     ambient + diffuse x max(n.k, 0) + specular x max(n.h, 0)^specPower
//     + rim x (1 - n.z)^2
//   fluid (parts G 2, Sandbox: water), its own darker rule, the glint only:
//     waterBase + waterGlint x max(n.h, 0)^waterPower
// with k the key light (azimuth from the top of the image, clockwise;
// elevation above the ground), h = normalize(k + v), and the normal's
// slope scaled by `relief` first. Output: R = brightness (raw: the contrast
// stretch is applied in asciiDecide, to ground and life only), G = 1 if the
// cell is fluid, B = 1 if any pixel had a normal, A = 1.
// Cells are at most 16 x 16 pixels.
// Keep in sync with ../GL3/asciiLit.frag.
#version 120
#extension GL_ARB_texture_rectangle : enable

uniform sampler2DRect surfaceTex;  // normals (RGB), A = 1 where drawn
uniform sampler2DRect partsTex;    // parts source
uniform sampler2DRect cellPartTex; // pass 1
uniform vec2 srcSize;              // output pixels
uniform vec2 surfaceScale;         // surface pixels per output pixel
uniform vec2 partsScale;           // parts pixels per output pixel
uniform vec2 cellSize;             // pixels (w, h), <= 16
uniform vec3 keyDir;               // unit, towards the light (x right, y down, z up)
uniform vec3 halfDir;              // unit, normalize(keyDir + (0, 0, 1))
uniform float relief;              // slope exaggeration
uniform float ambient;
uniform float diffuse;
uniform float specular;
uniform float specPower;
uniform float rim;
uniform float waterBase;
uniform float waterGlint;
uniform float waterPower;

varying vec2 coord;                  // cell-grid coordinate

float partKey(vec4 p)
{
	vec4 q = floor(p * 255.0 + 0.5);
	return (q.a < 0.5) ? -1.0 : q.r * 65536.0 + q.g * 256.0 + q.b;
}

void main()
{
	vec2 cell = floor(coord);
	vec2 origin = cell * cellSize;
	vec4 cp = texture2DRect(cellPartTex, cell + 0.5);
	float own = partKey(cp);
	bool fluid = floor(cp.g * 255.0 + 0.5) > 1.5;
	float sumOwn = 0.0, nOwn = 0.0, sumAll = 0.0, nAll = 0.0;
	for (int y = 0; y < 16; y++) {
		if (float(y) >= cellSize.y)
			break;
		for (int x = 0; x < 16; x++) {
			if (float(x) >= cellSize.x)
				break;
			vec2 p = origin + vec2(float(x), float(y));
			if (p.x >= srcSize.x || p.y >= srcSize.y)
				continue;
			vec4 s = texture2DRect(surfaceTex, (p + 0.5) * surfaceScale);
			if (s.a < 0.5)
				continue;
			vec3 n = normalize(vec3(s.xy * relief, max(s.z, 1e-3)));
			float nh = max(dot(n, halfDir), 0.0);
			float lit;
			if (fluid)
				lit = waterBase + waterGlint * pow(nh, waterPower);
			else
				lit = ambient + diffuse * max(dot(n, keyDir), 0.0) + specular * pow(nh, specPower)
					+ rim * (1.0 - n.z) * (1.0 - n.z);
			sumAll += lit;
			nAll += 1.0;
			if (partKey(texture2DRect(partsTex, (p + 0.5) * partsScale)) == own) {
				sumOwn += lit;
				nOwn += 1.0;
			}
		}
	}
	float lit = (nOwn > 0.0) ? sumOwn / nOwn : ((nAll > 0.0) ? sumAll / nAll : 0.0);
	gl_FragColor = vec4(lit, fluid ? 1.0 : 0.0, (nAll > 0.0) ? 1.0 : 0.0, 1.0);
}
