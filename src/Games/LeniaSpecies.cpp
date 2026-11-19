#include "LeniaSpecies.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>

namespace lenia {

// ------------------------------------------------------------ minimal JSON
// Enough JSON for species.json (objects, arrays, strings with the usual
// escapes, numbers, true/false/null). No dependency, so the library
// compiles headless.
namespace {

struct JVal {
	enum Type { NUL, BOOL, NUM, STR, ARR, OBJ } type = NUL;
	bool b = false;
	double n = 0;
	std::string s;
	std::vector<JVal> a;
	std::vector<std::pair<std::string, JVal>> o;
	const JVal * get(const std::string & key) const {
		if (type != OBJ) return nullptr;
		for (const auto & kv : o) if (kv.first == key) return &kv.second;
		return nullptr;
	}
};

struct JParser {
	const std::string & t;
	size_t i = 0;
	std::string err;
	explicit JParser(const std::string & text) : t(text) {}
	void ws() { while (i < t.size() && std::isspace((unsigned char)t[i])) i++; }
	bool fail(const std::string & m) { if (err.empty()) err = m + " at byte " + std::to_string(i); return false; }
	bool lit(const char * w) { size_t n = std::char_traits<char>::length(w); if (t.compare(i, n, w) != 0) return false; i += n; return true; }
	bool str(std::string & out) {
		if (i >= t.size() || t[i] != '"') return fail("expected string");
		i++;
		while (i < t.size() && t[i] != '"') {
			char c = t[i++];
			if (c == '\\') {
				if (i >= t.size()) return fail("bad escape");
				char e = t[i++];
				switch (e) {
				case 'n': out += '\n'; break;
				case 't': out += '\t'; break;
				case 'r': out += '\r'; break;
				case 'b': out += '\b'; break;
				case 'f': out += '\f'; break;
				case 'u': {
					if (i + 4 > t.size()) return fail("bad \\u escape");
					unsigned cp = (unsigned)std::strtoul(t.substr(i, 4).c_str(), nullptr, 16);
					i += 4;
					// UTF-8 (basic plane only; surrogate pairs are kept as two code points)
					if (cp < 0x80) out += (char)cp;
					else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
					else { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
					break;
				}
				default: out += e; break;
				}
			} else {
				out += c;
			}
		}
		if (i >= t.size()) return fail("unterminated string");
		i++;
		return true;
	}
	bool val(JVal & v, int depth = 0) {
		if (depth > 64) return fail("nesting too deep");
		ws();
		if (i >= t.size()) return fail("unexpected end");
		char c = t[i];
		if (c == '{') {
			v.type = JVal::OBJ;
			i++; ws();
			if (i < t.size() && t[i] == '}') { i++; return true; }
			for (;;) {
				ws();
				std::string k;
				if (!str(k)) return false;
				ws();
				if (i >= t.size() || t[i] != ':') return fail("expected ':'");
				i++;
				JVal x;
				if (!val(x, depth + 1)) return false;
				v.o.emplace_back(std::move(k), std::move(x));
				ws();
				if (i < t.size() && t[i] == ',') { i++; continue; }
				if (i < t.size() && t[i] == '}') { i++; return true; }
				return fail("expected ',' or '}'");
			}
		}
		if (c == '[') {
			v.type = JVal::ARR;
			i++; ws();
			if (i < t.size() && t[i] == ']') { i++; return true; }
			for (;;) {
				JVal x;
				if (!val(x, depth + 1)) return false;
				v.a.push_back(std::move(x));
				ws();
				if (i < t.size() && t[i] == ',') { i++; continue; }
				if (i < t.size() && t[i] == ']') { i++; return true; }
				return fail("expected ',' or ']'");
			}
		}
		if (c == '"') { v.type = JVal::STR; return str(v.s); }
		if (lit("true")) { v.type = JVal::BOOL; v.b = true; return true; }
		if (lit("false")) { v.type = JVal::BOOL; v.b = false; return true; }
		if (lit("null")) { v.type = JVal::NUL; return true; }
		if (c == '-' || std::isdigit((unsigned char)c)) {
			const char * s0 = t.c_str() + i;
			char * e = nullptr;
			v.type = JVal::NUM;
			v.n = std::strtod(s0, &e);
			if (e == s0) return fail("bad number");
			i += (size_t)(e - s0);
			return true;
		}
		return fail(std::string("unexpected '") + c + "'");
	}
};

double num(const JVal * v, double def) { return (v && v->type == JVal::NUM) ? v->n : def; }
std::string text(const JVal * v, const std::string & def = "") { return (v && v->type == JVal::STR) ? v->s : def; }

// "1/2,1" or [0.5, 1] or 1
bool fractions(const JVal * v, std::vector<double> & out, std::string & err) {
	out.clear();
	if (!v) return true;
	if (v->type == JVal::NUM) { out.push_back(v->n); return true; }
	if (v->type == JVal::ARR) {
		for (const auto & x : v->a) {
			if (x.type != JVal::NUM) { err = "shell weights: array of numbers expected"; return false; }
			out.push_back(x.n);
		}
		return true;
	}
	if (v->type == JVal::STR) {
		std::stringstream ss(v->s);
		std::string part;
		while (std::getline(ss, part, ',')) {
			size_t slash = part.find('/');
			double x = slash == std::string::npos ? std::atof(part.c_str())
				: std::atof(part.substr(0, slash).c_str()) / std::max(1e-12, std::atof(part.substr(slash + 1).c_str()));
			out.push_back(x);
		}
		return true;
	}
	err = "shell weights: unexpected type";
	return false;
}

bool triple(const JVal * v, double out[3], const char * what, std::string & err) {
	if (!v || v->type != JVal::ARR || v->a.size() != 3) { err = std::string(what) + ": three numbers expected"; return false; }
	for (int j = 0; j < 3; j++) out[j] = v->a[j].n;
	return true;
}

bool readKernel(const JVal & kv, Kernel & k, std::string & err) {
	if (kv.type != JVal::OBJ) { err = "kernel: object expected"; return false; }
	k.from = (int)num(kv.get("from"), 0);
	k.to = (int)num(kv.get("to"), 0);
	std::string shape = text(kv.get("shape"), "shells");
	if (shape == "shells") {
		k.shape = SHAPE_SHELLS;
		if (!fractions(kv.get("b"), k.b, err)) return false;
		if (k.b.empty()) k.b = { 1.0 };
		k.kn = (int)num(kv.get("kn"), CORE_POLY);
	} else if (shape == "rings") {
		k.shape = SHAPE_RINGS;
		if (!triple(kv.get("a"), k.ra, "rings a", err) || !triple(kv.get("b"), k.rb, "rings b", err) || !triple(kv.get("w"), k.rw, "rings w", err))
			return false;
	} else {
		err = "kernel shape '" + shape + "' unknown (shells | rings)";
		return false;
	}
	k.r = num(kv.get("r"), 1.0);
	k.R = num(kv.get("R"), -1.0);   // -1: the species' (or target channel's) R
	k.m = num(kv.get("m"), 0.15);
	k.s = num(kv.get("s"), 0.015);
	k.h = num(kv.get("h"), 1.0);
	k.gn = (int)num(kv.get("gn"), GROWTH_GAUSS);
	const JVal * rel = kv.get("relative");
	k.relative = rel && rel->type == JVal::BOOL && rel->b;
	if (k.kn < 1 || k.kn > 3) { err = "kn must be 1..3"; return false; }
	if (k.gn < 1 || k.gn > 3) { err = "gn must be 1..3"; return false; }
	if (k.s <= 0) { err = "s must be > 0"; return false; }
	return true;
}

} // namespace

int Library::find(const std::string & id) const
{
	for (size_t i = 0; i < species.size(); i++)
		if (species[i].id == id) return (int)i;
	return -1;
}

int Library::findWorld(const std::string & id) const
{
	for (size_t i = 0; i < worlds.size(); i++)
		if (worlds[i].id == id) return (int)i;
	return -1;
}

bool parseLibrary(const std::string & jsonText, Library & out, std::string & err)
{
	JParser p(jsonText);
	JVal root;
	if (!p.val(root)) { err = "species.json: " + p.err; return false; }
	if (root.type != JVal::OBJ) { err = "species.json: top level must be an object"; return false; }
	const JVal * sp = root.get("species");
	if (!sp || sp->type != JVal::ARR) { err = "species.json: 'species' array missing"; return false; }
	Library lib;
	for (const auto & sv : sp->a) {
		Species s;
		s.id = text(sv.get("id"));
		s.name = text(sv.get("name"), s.id);
		s.status = text(sv.get("status"));
		s.source = text(sv.get("source"));
		s.notes = text(sv.get("notes"));
		const std::string where = "species '" + s.id + "': ";
		if (s.id.empty()) { err = "species.json: a species has no id"; return false; }
		if (s.status != "published" && s.status != "tuned" && s.status != "sampled") {
			err = where + "status must be published, tuned or sampled"; return false;
		}
		if (s.source.empty()) { err = where + "source missing"; return false; }
		std::string model = text(sv.get("model"), "lenia");
		if (model == "lenia") s.model = MODEL_LENIA;
		else if (model == "flow") s.model = MODEL_FLOW;
		else { err = where + "model must be lenia or flow"; return false; }
		s.R = num(sv.get("R"), 13);
		if (s.model == MODEL_LENIA) {
			double T = num(sv.get("T"), 10);
			s.dt = T > 0 ? 1.0 / T : 0.1;
		} else {
			s.dt = num(sv.get("dt"), 0.2);
		}
		s.dd = (int)num(sv.get("dd"), 5);
		s.sigma = num(sv.get("sigma"), 0.65);
		s.theta = num(sv.get("theta"), 1.0);
		s.noisePatch = (int)num(sv.get("noisePatch"), 0);
		const JVal * ks = sv.get("kernels");
		if (!ks || ks->type != JVal::ARR || ks->a.empty()) { err = where + "kernels missing"; return false; }
		int maxCh = 0;
		for (const auto & kv : ks->a) {
			Kernel k;
			if (!readKernel(kv, k, err)) { err = where + err; return false; }
			if (k.R <= 0) k.R = s.R;
			maxCh = std::max(maxCh, std::max(k.from, k.to) + 1);
			s.kernels.push_back(k);
		}
		s.channels = (int)num(sv.get("channels"), maxCh);
		if (s.channels < 1 || s.channels > MAX_CHANNELS || maxCh > s.channels) { err = where + "channels out of range"; return false; }
		if ((int)s.kernels.size() > MAX_KERNELS) { err = where + "too many kernels"; return false; }
		const JVal * cells = sv.get("cells");
		if (cells) {
			std::vector<std::string> rles;
			if (cells->type == JVal::STR) rles.push_back(cells->s);
			else if (cells->type == JVal::ARR) for (const auto & c : cells->a) rles.push_back(c.s);
			for (const auto & r : rles) {
				Grid g;
				if (!decodeRLE(r, g, err)) { err = where + err; return false; }
				s.cells.push_back(g);
			}
			// Channels of a multi-channel pattern share one size (pad).
			int W = 0, H = 0;
			for (const auto & g : s.cells) { W = std::max(W, g.w); H = std::max(H, g.h); }
			for (auto & g : s.cells) {
				if (g.w == W && g.h == H) continue;
				Grid p2; p2.w = W; p2.h = H; p2.v.assign((size_t)W * H, 0.0f);
				for (int y = 0; y < g.h; y++) for (int x = 0; x < g.w; x++) p2.v[(size_t)y * W + x] = g.at(x, y);
				g = p2;
			}
			if ((int)s.cells.size() != s.channels) { err = where + "one cells pattern per channel expected"; return false; }
		}
		if (lib.find(s.id) >= 0) { err = where + "duplicate id"; return false; }
		lib.species.push_back(s);
	}
	const JVal * ws = root.get("worlds");
	if (ws && ws->type == JVal::ARR) {
		for (const auto & wv : ws->a) {
			WorldDef w;
			w.id = text(wv.get("id"));
			w.name = text(wv.get("name"), w.id);
			w.status = text(wv.get("status"));
			w.source = text(wv.get("source"));
			w.notes = text(wv.get("notes"));
			const std::string where = "world '" + w.id + "': ";
			if (w.status != "published" && w.status != "tuned" && w.status != "sampled") { err = where + "status must be published, tuned or sampled"; return false; }
			if (w.source.empty()) { err = where + "source missing"; return false; }
			w.model = text(wv.get("model"), "lenia") == "flow" ? MODEL_FLOW : MODEL_LENIA;
			const JVal * em = wv.get("embed");
			w.embed = em && em->type == JVal::BOOL && em->b;
			const JVal * ch = wv.get("channels");
			if (!ch || ch->type != JVal::ARR || ch->a.empty() || (int)ch->a.size() > MAX_CHANNELS) { err = where + "channels: 1..3 species ids"; return false; }
			for (const auto & c : ch->a) {
				std::string id = c.type == JVal::STR ? c.s : "";
				if (!id.empty() && lib.find(id) < 0) { err = where + "unknown species '" + id + "'"; return false; }
				w.channelSpecies.push_back(id);
			}
			const JVal * xk = wv.get("crossKernels");
			if (xk && xk->type == JVal::ARR)
				for (const auto & kv : xk->a) {
					Kernel k;
					if (!readKernel(kv, k, err)) { err = where + err; return false; }
					if (k.from >= (int)w.channelSpecies.size() || k.to >= (int)w.channelSpecies.size()) { err = where + "cross kernel channel out of range"; return false; }
					w.crossKernels.push_back(k);
				}
			lib.worlds.push_back(w);
		}
	}
	out = std::move(lib);
	return true;
}

bool loadLibraryFile(const std::string & path, Library & out, std::string & err)
{
	std::ifstream f(path, std::ios::binary);
	if (!f) { err = "cannot open " + path; return false; }
	std::stringstream ss;
	ss << f.rdbuf();
	return parseLibrary(ss.str(), out, err);
}

// ------------------------------------------------------------ RLE
bool decodeRLE(const std::string & rleIn, Grid & g, std::string & err)
{
	std::vector<std::vector<float>> rows;
	std::vector<float> row;
	std::string count;
	char last = 0;
	std::string rle = rleIn;
	while (!rle.empty() && (rle.back() == '!' || std::isspace((unsigned char)rle.back()))) rle.pop_back();
	rle += '$';
	for (char ch : rle) {
		if (std::isspace((unsigned char)ch)) continue;
		if (std::isdigit((unsigned char)ch)) { count += ch; continue; }
		if ((ch >= 'p' && ch <= 'y') || ch == '@') { last = ch; continue; }
		const int n = count.empty() ? 1 : std::atoi(count.c_str());
		if (n <= 0 || n > 100000) { err = "RLE: bad run count"; return false; }
		if (ch == '$' && last == 0) {
			rows.push_back(row);
			for (int k = 1; k < n; k++) rows.emplace_back();
			row.clear();
		} else {
			int v;
			if (last == 0) {
				if (ch == '.' || ch == 'b') v = 0;
				else if (ch == 'o') v = 255;
				else if (ch >= 'A' && ch <= 'X') v = ch - 'A' + 1;
				else { err = std::string("RLE: unexpected '") + ch + "'"; return false; }
			} else {
				if (last == '@' || ch < 'A' || ch > 'X') { err = "RLE: unexpected two-character value"; return false; }
				v = (last - 'p') * 24 + (ch - 'A' + 25);
			}
			if (v > 255) { err = "RLE: value above 255"; return false; }
			row.insert(row.end(), (size_t)n, (float)(v / 255.0));
		}
		count.clear();
		last = 0;
	}
	size_t W = 0;
	for (const auto & r : rows) W = std::max(W, r.size());
	g.w = (int)W;
	g.h = (int)rows.size();
	g.v.assign(W * rows.size(), 0.0f);
	for (size_t y = 0; y < rows.size(); y++)
		std::copy(rows[y].begin(), rows[y].end(), g.v.begin() + y * W);
	if (g.w == 0 || g.h == 0) { err = "RLE: empty pattern"; return false; }
	return true;
}

// ------------------------------------------------------------ kernels
double coreFn(int kn, double q)
{
	if (q <= 0.0 || q >= 1.0) return kn == CORE_STEP ? 0.0 : 0.0;
	switch (kn) {
	case CORE_POLY: { double v = 4.0 * q * (1.0 - q); return v * v * v * v; }
	case CORE_EXP: return std::exp(4.0 - 1.0 / (q * (1.0 - q)));
	case CORE_STEP: return (q >= 0.25 && q <= 0.75) ? 1.0 : 0.0;
	}
	return 0.0;
}

double growthFn(int gn, double u, double m, double s)
{
	const double d = u - m;
	switch (gn) {
	case GROWTH_POLY: { double v = std::max(0.0, 1.0 - d * d / (9.0 * s * s)); v = v * v; return 2.0 * v * v - 1.0; }
	case GROWTH_GAUSS: return 2.0 * std::exp(-d * d / (2.0 * s * s)) - 1.0;
	case GROWTH_STEP: return std::fabs(d) <= s ? 1.0 : -1.0;
	}
	return -1.0;
}

static double shellsValue(const Kernel & k, double q)
{
	// LeniaNDKC kernel_shell: Br = B q / r; b[min(floor(Br), B-1)] * core(Br mod 1) for q < r
	if (q >= k.r) return 0.0;
	const int B = (int)k.b.size();
	const double Br = B * q / k.r;
	const int idx = std::min((int)std::floor(Br), B - 1);
	double frac = Br - std::floor(Br);
	return k.b[idx] * coreFn(k.kn, std::min(frac, 1.0));
}

static double ringsValue(const Kernel & k, double D)
{
	double sum = 0.0;
	for (int j = 0; j < 3; j++) {
		const double e = D - k.ra[j];
		sum += k.rb[j] * std::exp(-e * e / std::max(1e-9, k.rw[j]));
	}
	return 0.5 * (std::tanh(-(D - 1.0) * 10.0 / 2.0) + 1.0) * sum;
}

TapKernel buildKernel(const Kernel & k, double R, double rScale)
{
	TapKernel t;
	const double scale = std::max(0.05, rScale);
	double cut;     // radius in cells beyond which taps are dropped
	double unit;    // cells per unit of q (shells) or D (rings)
	if (k.shape == SHAPE_SHELLS) {
		unit = R * scale;
		cut = unit * k.r;
	} else {
		unit = (R + 15.0) * k.r * scale;
		cut = unit * RINGS_CUT;
	}
	t.radius = std::max(1, (int)std::ceil(cut));
	const int n = 2 * t.radius + 1;
	t.w.assign((size_t)n * n, 0.0f);
	double kept = 0.0;
	for (int dy = -t.radius; dy <= t.radius; dy++)
		for (int dx = -t.radius; dx <= t.radius; dx++) {
			const double d = std::sqrt((double)dx * dx + (double)dy * dy);
			double v = 0.0;
			if (d <= cut) v = k.shape == SHAPE_SHELLS ? shellsValue(k, d / unit) : ringsValue(k, d / unit);
			t.w[(size_t)(dy + t.radius) * n + dx + t.radius] = (float)v;
			kept += v;
		}
	if (k.shape == SHAPE_RINGS) {
		// Weight beyond the cut, on a support twice as wide (the published
		// code normalises over the whole world).
		double total = kept;
		const int R2 = (int)std::ceil(unit * RINGS_CUT * 2.0);
		for (int dy = -R2; dy <= R2; dy++)
			for (int dx = -R2; dx <= R2; dx++) {
				const double d = std::sqrt((double)dx * dx + (double)dy * dy);
				if (d > cut) total += ringsValue(k, d / unit);
			}
		t.truncated = total > 0 ? (total - kept) / total : 0.0;
	}
	if (kept > 0)
		for (auto & w : t.w) w = (float)(w / kept);
	t.effectiveRadius = cut;
	return t;
}

bool buildTapTable(const std::vector<TapKernel> & ks, TapTable & t, std::string & err)
{
	t = TapTable();
	if (ks.empty()) { err = "no kernels"; return false; }
	if ((int)ks.size() > MAX_KERNELS) { err = "more than " + std::to_string(MAX_KERNELS) + " kernels"; return false; }
	t.kernels = (int)ks.size();
	for (const auto & k : ks) t.maxRadius = std::max(t.maxRadius, k.radius);
	const int r = t.maxRadius;
	for (int dy = -r; dy <= r; dy++)
		for (int dx = -r; dx <= r; dx++) {
			bool any = false;
			for (const auto & k : ks) if (k.at(dx, dy) != 0.0f) { any = true; break; }
			if (!any) continue;
			if (t.count >= MAX_TAPS) { err = "kernel too large for the GPU tap table (reduce R scale)"; return false; }
			t.dx.push_back(dx);
			t.dy.push_back(dy);
			for (int j = 0; j < MAX_KERNELS; j++)
				t.w.push_back(j < (int)ks.size() ? ks[j].at(dx, dy) : 0.0f);
			t.count++;
		}
	return true;
}

Grid transformGrid(const Grid & g, double scale, double deg)
{
	Grid o;
	if (g.w == 0 || g.h == 0) return o;
	scale = std::max(0.05, scale);
	const double a = deg * 3.14159265358979323846 / 180.0, c = std::cos(a), s = std::sin(a);
	const double hw = g.w * 0.5 * scale, hh = g.h * 0.5 * scale;
	const double ext = std::ceil(std::sqrt(hw * hw + hh * hh)) + 1;
	o.w = o.h = (int)(2 * ext);
	o.v.assign((size_t)o.w * o.h, 0.0f);
	for (int y = 0; y < o.h; y++)
		for (int x = 0; x < o.w; x++) {
			// output cell centre -> pattern coordinates (inverse rotation, then scale)
			const double px = x + 0.5 - ext, py = y + 0.5 - ext;
			const double ux = (c * px + s * py) / scale + g.w * 0.5 - 0.5;
			const double uy = (-s * px + c * py) / scale + g.h * 0.5 - 0.5;
			const int x0 = (int)std::floor(ux), y0 = (int)std::floor(uy);
			const double fx = ux - x0, fy = uy - y0;
			const double v = (1 - fx) * (1 - fy) * g.at(x0, y0) + fx * (1 - fy) * g.at(x0 + 1, y0)
				+ (1 - fx) * fy * g.at(x0, y0 + 1) + fx * fy * g.at(x0 + 1, y0 + 1);
			o.v[(size_t)y * o.w + x] = (float)v;
		}
	return o;
}

// ------------------------------------------------------------ world
bool assembleWorld(const Library & lib, const WorldConfig & cfg, WorldSetup & out, std::string & err)
{
	WorldSetup w;
	w.model = cfg.model;
	w.embed = cfg.model == MODEL_FLOW && cfg.embed;
	bool first = true;
	int used = 0;   // channels taken so far (a multi-channel species takes several)
	for (int c = 0; c < MAX_CHANNELS; c++) {
		const std::string & id = cfg.channelSpecies[c];
		if (id.empty()) continue;
		const int si = lib.find(id);
		if (si < 0) { err = "unknown species '" + id + "'"; return false; }
		const Species & sp = lib.species[si];
		if (sp.model != cfg.model) { err = "'" + sp.name + "' is a " + (sp.model == MODEL_FLOW ? "Flow-Lenia" : "Lenia") + " species"; return false; }
		const int base = w.embed ? 0 : c;
		if (w.embed && sp.channels != 1) { err = "embedding needs one-channel species ('" + sp.name + "' has " + std::to_string(sp.channels) + ")"; return false; }
		if (!w.embed && (base < used || base + sp.channels > MAX_CHANNELS)) {
			err = "'" + sp.name + "' needs channels " + std::to_string(base) + ".." + std::to_string(base + sp.channels - 1) + " (taken or out of range)";
			return false;
		}
		for (Kernel k : sp.kernels) {
			k.from += base;
			k.to += base;
			w.kernels.push_back(k);
			w.kernelSlot.push_back(w.embed ? c : -1);
			w.kernelOwner.push_back(c);
		}
		if (!w.embed) used = base + sp.channels;
		if (first) {
			w.dt = sp.dt;
			w.dd = sp.dd;
			w.sigma = sp.sigma;
			w.theta = sp.theta;
			first = false;
		}
	}
	for (Kernel k : cfg.crossKernels) {
		if (w.embed) { err = "cross kernels are not used with embedding"; return false; }
		if (k.from < 0 || k.to < 0 || k.from >= MAX_CHANNELS || k.to >= MAX_CHANNELS) { err = "cross kernel channel out of range"; return false; }
		if (k.R <= 0) {
			// the target channel's species R
			k.R = 13;
			for (size_t i = 0; i < w.kernels.size(); i++)
				if (w.kernels[i].to == k.to) { k.R = w.kernels[i].R; break; }
		}
		w.kernels.push_back(k);
		w.kernelSlot.push_back(-1);
		w.kernelOwner.push_back(-1);
	}
	if (w.kernels.empty()) { err = "no species selected"; return false; }
	if ((int)w.kernels.size() > MAX_KERNELS) { err = "more than " + std::to_string(MAX_KERNELS) + " kernels in this world"; return false; }
	out = w;
	return finalizeWorld(out, err);
}

static void setCol(float m[16], int col, int row, float v) { m[col * 4 + row] = v; }

bool finalizeWorld(WorldSetup & w, std::string & err)
{
	w.taps.clear();
	w.maxTruncated = 0.0;
	for (const Kernel & k : w.kernels) {
		if (k.s <= 0) { err = "s must be > 0"; return false; }
		w.taps.push_back(buildKernel(k, k.R, w.rScale));
		w.maxTruncated = std::max(w.maxTruncated, w.taps.back().truncated);
	}
	if (!buildTapTable(w.taps, w.table, err)) return false;
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 16; j++) { w.srcSel[i][j] = 0; w.dstSel[i][j] = 0; w.slotSel[i][j] = 0; }
	for (int c = 0; c < MAX_CHANNELS; c++) w.channelOn[c] = 0;
	for (int k = 0; k < MAX_KERNELS; k++) { w.m[k] = 0.5f; w.s[k] = 1.0f; w.h[k] = 0.0f; w.gn[k] = 2.0f; w.rel[k] = 0.0f; }
	for (size_t k = 0; k < w.kernels.size(); k++) {
		const Kernel & K = w.kernels[k];
		const int g = (int)k / 4, j = (int)k % 4;
		// (srcSel * a)[j] = a[from]: column `from`, row j
		setCol(w.srcSel[g], K.from, j, 1.0f);
		// (dstSel * hG)[to] += hG[j]: column j, row `to`
		setCol(w.dstSel[g], j, K.to, 1.0f);
		// (slotSel * P)[j] = P[slot]: column slot, row j
		if (w.embed && k < w.kernelSlot.size() && w.kernelSlot[k] >= 0)
			setCol(w.slotSel[g], w.kernelSlot[k], j, 1.0f);
		w.m[k] = (float)K.m;
		w.s[k] = (float)K.s;
		w.h[k] = (float)K.h;
		w.gn[k] = (float)K.gn;
		w.rel[k] = K.relative ? 1.0f : 0.0f;
		w.channelOn[K.to] = 1.0f;
	}
	return true;
}

} // namespace lenia
