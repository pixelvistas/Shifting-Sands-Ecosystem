// Prototype 4 (Scene), pass 1: the lit top-down scene in grid space at
// sceneMult x grid resolution (RGBA16F). Display only. Everything moves as
// smooth functions of animTime (wind, gusts, dappled light) or of the
// RippleField; nothing moves from sensor noise (the height is the smoothed
// display height). Keep in sync with ../GL3/sceneRender.frag;
// tools/scene_preview runs this file.
//
// Layers: terrain colour by height (three stops) lit by Lambert from the
// height normals + ambient, times a drifting dappled-light mask; grass
// blades (procedural, hashed tufts: see grassDensity() and grass()); water
// where the simulated depth exceeds waterThreshold: near-black teal by
// depth, ripple normals (specular + shimmer), a faint mirrored reflection
// of the land; then exposure (gain, toe and shoulder curve) so the useful
// range spans 0..1, then the black point: luminance below it is exactly 0.
// Heights are read as fractions of the active elevation range (rangeLo ..
// rangeHi, the "Elevation range" setting), so terrain stops and the grass
// band mean the same on the mock and on the sand. Output A = the final
// luminance (histogram and auto-exposure).
#version 120
#extension GL_ARB_texture_rectangle : enable

uniform sampler2DRect heightTex;   // grid: R display height (mm)
uniform sampler2DRect waterTex;    // grid: R depth (mm)
uniform sampler2DRect rippleTex;   // ripple grid: R u
uniform float sceneMult;           // scene texels per grid cell
uniform float rippleMult;          // ripple cells per grid cell
uniform vec2 gridSize;
uniform float mmPerCell;
uniform float animTime;            // s; stops while "Freeze animation" is on
// terrain and light
uniform float rangeLo;             // mm, active elevation range
uniform float rangeHi;
uniform float terrainLoFrac;       // low colour stop, fraction of the range
uniform float terrainHiFrac;       // high colour stop
uniform vec3 terrainLow;
uniform vec3 terrainMid;
uniform vec3 terrainHigh;
uniform float terrainMidPos;       // 0..1
uniform float exaggeration;        // height scale for the normals
uniform vec3 sunDir;               // unit, towards the sun (z up)
uniform vec3 sunColor;
uniform float ambient;
uniform float dappleScale;         // mm
uniform vec2 dappleVelocity;       // mm/s
uniform float dappleContrast;      // 0..1
uniform float dappleShade;         // light level in the shade, 0..1
// grass
uniform float bladeLength;         // mm (from the stylise units on the CPU)
uniform float bladeWidth;          // mm
uniform float tuftSpacing;         // mm
uniform float bladesPerTuft;       // 1..6
uniform float grassMinFrac;        // grass band, fractions of the range
uniform float grassMaxFrac;
uniform float grassMaxSlope;       // mm per mm
uniform float clumpScale;          // mm
uniform float clumpAmount;         // 0..1
uniform vec3 grassBase;
uniform vec3 grassTip;
uniform float tipBoost;
uniform vec2 windDir;              // unit
uniform float windStrength;        // tip offset, fraction of the blade length
uniform float windWaveLength;      // mm
uniform float windSpeed;           // waves per second
uniform float gustStrength;
uniform float leanSpread;          // radians around the wind direction
// water
uniform float waterThreshold;      // mm
uniform float waterEdge;           // mm of depth over which water fades in
uniform float waterDeepDepth;      // mm
uniform vec3 waterShallow;
uniform vec3 waterDeep;
uniform float rippleNormalGain;
uniform float specStrength;
uniform float shininess;
uniform float rippleShimmer;
uniform float highlightMax;        // water highlights compressed below this
uniform float reflectStrength;
uniform vec2 reflectOffset;        // mm
uniform float reflectAmp;          // mm
uniform float reflectFreq;         // 1/mm
uniform float reflectSpeed;        // rad/s
// output
uniform float exposure;            // gain (x auto-exposure)
uniform float toe;                 // >= 1: deepens the shadows
uniform float shoulder;            // >= 0: compresses the highlights
uniform float blackPoint;
uniform float blackSoftness;

varying vec2 coord;                     // scene texel coordinate

float hash12(vec2 p)
{
	vec3 p3 = fract(vec3(p.xyx) * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}

vec2 hash22(vec2 p)
{
	vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.xx + p3.yz) * p3.zy);
}

float valueNoise(vec2 p)
{
	vec2 i = floor(p);
	vec2 f = p - i;
	vec2 u = f * f * (3.0 - 2.0 * f);
	return mix(mix(hash12(i), hash12(i + vec2(1.0, 0.0)), u.x),
		mix(hash12(i + vec2(0.0, 1.0)), hash12(i + vec2(1.0, 1.0)), u.x), u.y);
}

float fbm(vec2 p)
{
	float v = 0.0, a = 0.5;
	for (int o = 0; o < 4; o++) {
		v += a * valueNoise(p);
		p = p * 2.03 + vec2(17.1, 9.2);
		a *= 0.5;
	}
	return v / 0.9375;
}

// Grid coordinate (cells) of a position in mm.
vec2 toGrid(vec2 pmm)
{
	return pmm / mmPerCell;
}

float heightAt(vec2 pmm)
{
	return texture2DRect(heightTex, toGrid(pmm)).r;
}

float depthAt(vec2 pmm)
{
	return texture2DRect(waterTex, toGrid(pmm)).r;
}

// Height gradient (mm per mm) by central differences one cell apart.
vec2 heightGrad(vec2 pmm)
{
	float e = mmPerCell;
	return vec2(heightAt(pmm + vec2(e, 0.0)) - heightAt(pmm - vec2(e, 0.0)),
		heightAt(pmm + vec2(0.0, e)) - heightAt(pmm - vec2(0.0, e))) / (2.0 * e);
}

// Drifting dappled light: 1 in sun, dappleShade in shade.
float dapple(vec2 pmm)
{
	float n = fbm((pmm - dappleVelocity * animTime) / max(1.0, dappleScale));
	float m = smoothstep(0.5 - 0.5 * dappleContrast, 0.5 + 0.5 * dappleContrast, n);
	return mix(dappleShade, 1.0, m);
}

// ---------------------------------------------------------------------
// grassDensity(): where grass grows, 0..1. THIS STEP: a visual rule only
// (an elevation band, a slope limit, a clumping noise; none on water).
// Step 2 replaces this one function with the succession model's cover.
// ---------------------------------------------------------------------
float heightFrac(float heightMM)
{
	return (heightMM - rangeLo) / max(1.0, rangeHi - rangeLo);
}

float grassDensity(float heightMM, float slope, float clump, float depthMM)
{
	if (depthMM > waterThreshold)
		return 0.0;
	float f = heightFrac(heightMM);
	float soft = 0.015;
	float band = smoothstep(grassMinFrac - soft, grassMinFrac + soft, f)
		* (1.0 - smoothstep(grassMaxFrac - soft, grassMaxFrac + soft, f));
	float slopeOk = 1.0 - smoothstep(0.8 * grassMaxSlope, grassMaxSlope, slope);
	return band * slopeOk * mix(1.0, clump, clumpAmount);
}

// Grass blades over position pmm: tufts on a tuftSpacing grid, up to 6
// blades each, every blade a tapered stroke from its root towards its
// tip, the tip swayed by a travelling wind wave plus gust noise. Tufts
// within the blades' reach are searched (at most 3 tufts each way).
// Returns the blade colour factor (rgb, unlit) and its coverage (a).
vec4 grass(vec2 pmm, float pxMM)
{
	float tuft = max(0.5, tuftSpacing);
	vec2 tc = floor(pmm / tuft);
	vec4 acc = vec4(0.0);
	vec2 windN = vec2(-windDir.y, windDir.x);
	float reach = min(3.0, ceil(bladeLength * 1.3 * (1.0 + windStrength * (1.0 + gustStrength)) / tuft));
	for (int j = -3; j <= 3; j++) {
		for (int i = -3; i <= 3; i++) {
			if (abs(float(i)) > reach || abs(float(j)) > reach)
				continue;
			vec2 cell = tc + vec2(float(i), float(j));
			vec2 centre = (cell + 0.5) * tuft;
			float h = heightAt(centre);
			float dens = grassDensity(h, length(heightGrad(centre)) * exaggeration,
				fbm(centre / max(1.0, clumpScale)), depthAt(centre));
			if (dens <= 0.001)
				continue;
			for (int b = 0; b < 6; b++) {
				if (float(b) >= bladesPerTuft)
					break;
				vec2 r = hash22(cell * 1.37 + vec2(float(b) * 7.7, float(b) * 3.1));
				if (hash12(cell + vec2(float(b) * 11.3, 5.0)) > dens)
					continue;
				vec2 root = (cell + r) * tuft;
				float ang = (hash12(cell * 2.1 + float(b)) - 0.5) * 2.0 * leanSpread;
				vec2 lean = cos(ang) * windDir + sin(ang) * windN;
				float wave = sin(6.2831853 * (dot(root, windDir) / max(1.0, windWaveLength) - windSpeed * animTime));
				float gust = fbm(root / 60.0 + windDir * animTime * 0.35) - 0.5;
				float len = bladeLength * (0.7 + 0.6 * hash12(cell + vec2(float(b), 2.0)));
				vec2 tip = root + lean * len * 0.6 + windDir * len * windStrength * (0.6 + 0.4 * wave + gustStrength * gust);
				vec2 ab = tip - root;
				float t = clamp(dot(pmm - root, ab) / max(1e-4, dot(ab, ab)), 0.0, 1.0);
				float d = length(pmm - root - ab * t);
				float w = 0.5 * bladeWidth * (1.0 - 0.7 * t);
				float cov = 1.0 - smoothstep(w - 0.5 * pxMM, w + 0.5 * pxMM, d);
				if (cov > acc.a) {
					vec3 c = mix(grassBase, grassTip, t) * mix(1.0, tipBoost, t * t);
					acc = vec4(c, cov);
				}
			}
		}
	}
	return acc;
}

vec3 sunlight(vec2 pmm)
{
	vec2 g = heightGrad(pmm) * exaggeration;
	vec3 n = normalize(vec3(-g, 1.0));
	return sunColor * (ambient + max(dot(n, sunDir), 0.0)) * dapple(pmm);
}

// Land at pmm: terrain colour and grass, lit.
vec3 landColour(vec2 pmm, float pxMM)
{
	float h = heightAt(pmm);
	float k = clamp((heightFrac(h) - terrainLoFrac) / max(0.01, terrainHiFrac - terrainLoFrac), 0.0, 1.0);
	vec3 base = (k < terrainMidPos) ? mix(terrainLow, terrainMid, k / max(1e-3, terrainMidPos))
		: mix(terrainMid, terrainHigh, (k - terrainMidPos) / max(1e-3, 1.0 - terrainMidPos));
	vec4 gr = grass(pmm, pxMM);
	return mix(base, gr.rgb, gr.a) * sunlight(pmm);
}

void main()
{
	vec2 g = coord / sceneMult;            // grid cells
	vec2 pmm = g * mmPerCell;
	float pxMM = mmPerCell / sceneMult;    // mm per scene texel
	vec3 col = landColour(pmm, pxMM);

	float depth = depthAt(pmm);
	float wa = smoothstep(waterThreshold, waterThreshold + max(0.01, waterEdge), depth);
	if (wa > 0.0) {
		vec2 rc = g * rippleMult;
		float du = texture2DRect(rippleTex, rc + vec2(1.0, 0.0)).r - texture2DRect(rippleTex, rc - vec2(1.0, 0.0)).r;
		float dv = texture2DRect(rippleTex, rc + vec2(0.0, 1.0)).r - texture2DRect(rippleTex, rc - vec2(0.0, 1.0)).r;
		vec2 slope = vec2(du, dv) * 0.5 * rippleNormalGain;
		vec3 n = normalize(vec3(-slope, 1.0));
		vec3 hv = normalize(sunDir + vec3(0.0, 0.0, 1.0));
		float spec = specStrength * pow(max(dot(n, hv), 0.0), max(1.0, shininess));
		vec3 water = mix(waterShallow, waterDeep, clamp(depth / max(0.1, waterDeepDepth), 0.0, 1.0));
		float hl = spec + rippleShimmer * length(slope);
		hl = highlightMax * (1.0 - exp(-hl / max(0.01, highlightMax)));
		water += sunColor * hl;
		vec2 off = reflectOffset + reflectAmp * vec2(sin(pmm.y * reflectFreq + animTime * reflectSpeed),
			sin(pmm.x * reflectFreq * 1.3 + animTime * reflectSpeed * 0.8));
		if (reflectStrength > 0.0)
			water += reflectStrength * landColour(pmm + off, pxMM);
		col = mix(col, water, wa);
	}

	// Exposure: gain, shoulder (x (1 + s) / (1 + s x): 1 stays 1), toe (power).
	col = max(col * exposure, vec3(0.0));
	col = col * (1.0 + shoulder) / (1.0 + shoulder * col);
	col = pow(clamp(col, 0.0, 1.0), vec3(max(1.0, toe)));
	float lum = dot(col, vec3(0.2126, 0.7152, 0.0722));
	col *= (blackSoftness > 0.0) ? smoothstep(blackPoint, blackPoint + blackSoftness, lum) : step(blackPoint, lum);
	gl_FragColor = vec4(col, dot(col, vec3(0.2126, 0.7152, 0.0722)));
}
