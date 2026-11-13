// Prototype 2 (Mycorrhiza): the trace drawn as a glowing fibrous network
// on black. Shares no code with heightMapShader. Keep in sync with
// ../GL3/mycorrhiza.frag; tools/mycorrhiza_harness/main.cpp (renderFrame)
// is a CPU copy used for the headless GIF.
//
// traceTex (rectangle, 8 bit): round(sqrt(clamp(trace / level)) x 255),
// uploaded by MycorrhizaRenderer; decoded as (e x e)^gamma.
#version 120
#extension GL_ARB_texture_rectangle : enable

uniform sampler2DRect traceTex;
uniform float gamma;        // display gamma on the normalised trace
uniform float coreGain;     // core brightness = ramp(i) x min(1, i x coreGain)
uniform float glowRadius;   // texels
uniform float glowStrength;
uniform float brightness;   // "Mycorrhiza brightness"
uniform float rampPos[5];   // increasing, 0 .. 1
uniform vec3 rampColor[5];  // deep indigo, violet, magenta, cyan, warm highlight

varying vec2 traceCoord;

float traceAt(vec2 c)
{
	float e = texture2DRect(traceTex, c).r;
	return pow(e * e, gamma);
}

vec3 ramp(float t)
{
	t = clamp(t, 0.0, 1.0);
	for (int s = 0; s < 3; s++) {
		if (t <= rampPos[s + 1]) {
			float u = clamp((t - rampPos[s]) / max(rampPos[s + 1] - rampPos[s], 1e-4), 0.0, 1.0);
			return mix(rampColor[s], rampColor[s + 1], u);
		}
	}
	float u = clamp((t - rampPos[3]) / max(rampPos[4] - rampPos[3], 1e-4), 0.0, 1.0);
	return mix(rampColor[3], rampColor[4], u);
}

void main()
{
	float core = traceAt(traceCoord);
	// Ring glow: 8 taps at glowRadius (weight 1) and 8 at 2 x glowRadius (weight 0.5).
	float g = 0.0;
	for (int d = 0; d < 8; d++) {
		float a = 0.7853982 * float(d);
		vec2 dir = vec2(cos(a), sin(a));
		g += traceAt(traceCoord + dir * glowRadius);
		g += 0.5 * traceAt(traceCoord + dir * 2.0 * glowRadius);
	}
	g /= 12.0;
	vec3 col = ramp(core) * min(1.0, core * coreGain) + glowStrength * ramp(g) * g;
	gl_FragColor = vec4(min(col * brightness, vec3(1.0)), 1.0);
}
