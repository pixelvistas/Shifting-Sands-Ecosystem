// Prototype 2 (Mycorrhiza): the trace drawn as a glowing fibrous network
// on black. Shares no code with heightMapShader. Keep in sync with
// ../GL3/mycorrhiza.frag; tools/mycorrhiza_harness/main.cpp (renderFrame)
// is a CPU copy used for the headless GIF.
//
// Brightness from the trace (traceTex, 8 bit: round(sqrt(clamp(trace /
// level)) x 255), decoded (e x e)^gamma, sharpened by local contrast);
// hue from the site under it: cyan where wet, magenta where living, warm
// orange on high dry ground, over a violet base. No green, so no
// red-green adjacency.
#version 120
#extension GL_ARB_texture_rectangle : enable

uniform sampler2DRect traceTex;
uniform sampler2DRect siteTex;  // R moisture, G life = max(cover, soil), B height (MycorrhizaField::site)
uniform float gamma;        // display gamma on the normalised trace
uniform float coreGain;     // core brightness = hue x min(1, i x coreGain)
uniform float detail;       // local contrast: i = max(0, i - detail x ring average) / (1 - detail)
uniform float glowRadius;   // texels
uniform float glowStrength;
uniform float brightness;   // "Mycorrhiza brightness"
uniform float baseWeight;   // weight of the base colour in the hue mix
uniform float wetLo;        // moisture where cyan starts
uniform float wetHi;        // moisture where cyan is full
uniform float coreMix;      // pale highlight at the strongest strands, x i^4
uniform vec3 colBase;       // violet
uniform vec3 colWet;        // cyan
uniform vec3 colLife;       // magenta
uniform vec3 colHigh;       // warm orange (high, dry ground)
uniform vec3 colCore;       // pale highlight

varying vec2 traceCoord;

float traceAt(vec2 c)
{
	float e = texture2DRect(traceTex, c).r;
	return pow(e * e, gamma);
}

vec3 siteHue(vec3 site)
{
	float wWet = smoothstep(wetLo, wetHi, site.r);
	float wLife = site.g;
	float wHigh = site.b * (1.0 - site.r);
	return (colBase * baseWeight + colWet * wWet + colLife * wLife + colHigh * wHigh) / (baseWeight + wWet + wLife + wHigh);
}

void main()
{
	float core = traceAt(traceCoord);
	// Ring: 8 taps at glowRadius (weight 1) and 8 at 2 x glowRadius (weight 0.5).
	float g = 0.0;
	for (int d = 0; d < 8; d++) {
		float a = 0.7853982 * float(d);
		vec2 dir = vec2(cos(a), sin(a));
		g += traceAt(traceCoord + dir * glowRadius);
		g += 0.5 * traceAt(traceCoord + dir * 2.0 * glowRadius);
	}
	g /= 12.0;
	if (detail > 0.0)
		core = max(core - detail * g, 0.0) / (1.0 - min(detail, 0.95));
	vec3 hue = siteHue(texture2DRect(siteTex, traceCoord).rgb);
	float c = min(1.0, core * coreGain);
	float hi = coreMix * core * core * core * core;
	vec3 col = mix(hue, colCore, hi) * c + glowStrength * hue * g;
	gl_FragColor = vec4(min(col * brightness, vec3(1.0)), 1.0);
}
