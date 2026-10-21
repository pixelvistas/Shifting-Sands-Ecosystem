// Minimal dependency-free PNG writer (stored/uncompressed deflate), for the
// headless harness only.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace png {
inline uint32_t crc(const unsigned char * d, size_t n, uint32_t c = 0xffffffffu)
{
	static uint32_t t[256]; static bool init = false;
	if (!init) { for (uint32_t i = 0; i < 256; i++) { uint32_t v = i; for (int k = 0; k < 8; k++) v = v & 1 ? 0xedb88320u ^ (v >> 1) : v >> 1; t[i] = v; } init = true; }
	for (size_t i = 0; i < n; i++) c = t[(c ^ d[i]) & 0xff] ^ (c >> 8);
	return c;
}
inline void be32(std::vector<unsigned char> & o, uint32_t v) { o.push_back(v >> 24); o.push_back(v >> 16); o.push_back(v >> 8); o.push_back(v); }
inline void chunk(FILE * fp, const char * type, const std::vector<unsigned char> & data)
{
	std::vector<unsigned char> b; be32(b, (uint32_t)data.size());
	std::vector<unsigned char> td(type, type + 4); td.insert(td.end(), data.begin(), data.end());
	b.insert(b.end(), td.begin(), td.end()); be32(b, crc(td.data(), td.size()) ^ 0xffffffffu);
	fwrite(b.data(), 1, b.size(), fp);
}
// rgb: w*h*3 bytes, row-major.
inline bool writeRGB(const std::string & path, int w, int h, const std::vector<unsigned char> & rgb)
{
	FILE * fp = fopen(path.c_str(), "wb"); if (!fp) return false;
	const unsigned char sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 }; fwrite(sig, 1, 8, fp);
	std::vector<unsigned char> ih; be32(ih, w); be32(ih, h); ih.push_back(8); ih.push_back(2); ih.push_back(0); ih.push_back(0); ih.push_back(0);
	chunk(fp, "IHDR", ih);
	std::vector<unsigned char> raw; for (int y = 0; y < h; y++) { raw.push_back(0); raw.insert(raw.end(), rgb.begin() + (size_t)y * w * 3, rgb.begin() + (size_t)(y + 1) * w * 3); }
	std::vector<unsigned char> z = { 0x78, 0x01 }; size_t pos = 0;
	uint32_t a = 1, b2 = 0; for (unsigned char c : raw) { a = (a + c) % 65521; b2 = (b2 + a) % 65521; }
	while (pos < raw.size()) { size_t n = std::min<size_t>(65535, raw.size() - pos); z.push_back(pos + n == raw.size() ? 1 : 0);
		z.push_back(n & 0xff); z.push_back(n >> 8); z.push_back(~n & 0xff); z.push_back((~n >> 8) & 0xff);
		z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n); pos += n; }
	be32(z, (b2 << 16) | a);
	chunk(fp, "IDAT", z); chunk(fp, "IEND", {}); fclose(fp); return true;
}
}
