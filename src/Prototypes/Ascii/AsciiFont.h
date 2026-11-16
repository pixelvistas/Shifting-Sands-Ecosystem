/***********************************************************************
AsciiFont - the 8 x 8 bitmap glyphs of Prototype 3 ("ASCII").

Plain C++ (no openFrameworks), so the glyph set can be checked headless.
Only the characters the ASCII display uses are defined (GLYPHS below);
each is an 8 x 8 bitmap drawn for this module, not taken from a font
file, so the display depends on no font being installed.

buildAtlas() writes one row of glyphs, left to right in GLYPHS order:
width = GLYPH_PX x glyphCount(), height = GLYPH_PX, one byte per pixel
(0 or 255), row 0 at the top. AsciiRenderer uploads it as a rectangle
texture and passes atlas indices (indexOf) to the shader.
***********************************************************************/

#pragma once
#include <string>
#include <vector>

namespace AsciiFont
{
	static const int GLYPH_PX = 8;

	// Every character the module can draw; indexOf() of any other
	// character returns the index of ' '.
	const std::string & glyphs();
	int glyphCount();
	int indexOf(char c);

	// width x height bytes, row-major, row 0 at the top.
	void buildAtlas(std::vector<unsigned char> & out, int & width, int & height);
}
