// Prototype 4 (Scene), display height step 1 (ping-pong, grid resolution):
// temporal exponential moving average of the raw height (display only;
// replaces Prototype 3's hysteresis). Output R = smoothed height (mm),
// A = 1 (0: not yet initialised). Keep in sync with
// ../GL3/sceneHeightEma.frag.
#version 120
#extension GL_ARB_texture_rectangle : enable

uniform sampler2DRect rawTex;   // R height (mm)
uniform sampler2DRect prevTex;
uniform float alpha;            // 1 - exp(-dt / time constant)
uniform int resetAll;

varying vec2 coord;

void main()
{
	float h = texture2DRect(rawTex, coord).r;
	vec4 p = texture2DRect(prevTex, coord);
	float s = (resetAll == 1 || p.a < 0.5) ? h : mix(p.r, h, alpha);
	gl_FragColor = vec4(s, 0.0, 0.0, 1.0);
}
