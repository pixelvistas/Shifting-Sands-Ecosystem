// AsciiFilter, writing marks: draws one mark layer (a texture the size of
// the output, or scaled to it) into the marks buffer. The layer's visible
// colour is RGB (premultiplied = 1: premultiplied or additive layers) or
// RGB x A (premultiplied = 0: straight alpha). Pixels whose brightest
// visible channel is at or below threshold are discarded, so a later layer
// replaces an earlier one only where it shows. Output: RGB = the visible
// colour, A = the layer's glyph (character code / 255).
// Keep in sync with ../GL2/asciiMarkWrite.frag.
#version 150

uniform sampler2DRect srcTex;      // the layer, in its own pixels
uniform vec2 srcScale;             // layer pixels per output pixel
uniform int premultiplied;
uniform float glyph;               // character code, 33..126
uniform float threshold;

in vec2 coord;                     // output pixel coordinate
out vec4 outputColor;

void main()
{
	vec4 t = texture(srcTex, coord * srcScale);
	vec3 c = (premultiplied == 1) ? t.rgb : t.rgb * t.a;
	c = clamp(c, 0.0, 1.0);
	if (max(c.r, max(c.g, c.b)) <= threshold)
		discard;
	outputColor = vec4(c, glyph / 255.0);
}
