#include "album_art.h"
#include "cache_manager.h"
#include "process_util.h"
#include "tiny_json.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <system_error>
#include <vector>

// stb_image is header-only: exactly one translation unit in the program
// must define STB_IMAGE_IMPLEMENTATION, and this is it (nothing else
// includes stb_image.h). Restricting it to JPEG+PNG drops the BMP/TGA/
// PSD/GIF/HDR/PIC/PNM decoders from the binary — cover art from the
// iTunes/MusicBrainz style endpoints is always one of those two, and the
// unused decoders are the bulk of stb_image's code size.
//
// Note there is deliberately NO STBI_NO_STDIO here: load_album_art works
// from a filename, so it needs stb's own fopen-based loader rather than
// us slurping the file into memory first.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#include "stb_image.h"

namespace muisc {

// --- decode + resample -------------------------------------------------

// Averages every source pixel that falls inside each target cell, rather
// than sampling one pixel per cell. Nearest-neighbour is the obvious
// shortcut and it looks fine on flat graphics, but cover art shrunk from
// ~600px to ~64px this way loses roughly 99% of its pixels: fine detail
// (text on a sleeve, film grain, dithered gradients) aliases into
// high-contrast speckle that the Braille/ASCII quantizer then amplifies
// into visual noise. Box-averaging keeps every source pixel's
// contribution, so what survives the reduction is the image's actual
// low-frequency structure — which is all a 64px grayscale square can
// show anyway.
//
// `src` is `side`x`side` starting at (ox, oy) inside a `src_w`-wide
// buffer; the crop is applied here instead of copying it out first.
static std::vector<unsigned char> box_downsample(const unsigned char* src, int src_w,
                                                  int ox, int oy, int side, int target) {
    std::vector<unsigned char> out(static_cast<size_t>(target) * target, 0);

    for (int ty = 0; ty < target; ++ty) {
        // Integer cell bounds: (ty * side) / target spreads the remainder
        // across cells instead of letting it pile up at the last row.
        int y0 = (ty * side) / target;
        int y1 = ((ty + 1) * side) / target;
        // Upscaling (target > side) leaves cells with no source pixel at
        // all; widening to a single pixel degrades to pixel replication
        // there rather than emitting a black row.
        if (y0 >= side) y0 = side - 1;
        if (y1 <= y0) y1 = std::min(y0 + 1, side);

        for (int tx = 0; tx < target; ++tx) {
            int x0 = (tx * side) / target;
            int x1 = ((tx + 1) * side) / target;
            if (x0 >= side) x0 = side - 1;
            if (x1 <= x0) x1 = std::min(x0 + 1, side);

            uint64_t sum = 0;
            uint64_t count = 0;
            for (int sy = y0; sy < y1; ++sy) {
                const unsigned char* row = src + static_cast<size_t>(oy + sy) * src_w + ox;
                for (int sx = x0; sx < x1; ++sx) {
                    sum += row[sx];
                    ++count;
                }
            }
            out[static_cast<size_t>(ty) * target + tx] =
                count ? static_cast<unsigned char>(sum / count) : 0;
        }
    }
    return out;
}

// The colour counterpart to box_downsample, and deliberately NOT an
// average. Averaging is the obvious thing to reach for and it is the wrong
// tool for colour: a cell straddling a red region and a blue one averages
// to a desaturated purple-grey, so a cover reduced that way arrives as
// grey mush with every edge smeared across the cells it crosses. Taking
// the single most prominent colour in each cell instead keeps saturation
// intact and keeps boundaries crisp, which is what lets a 30x30
// reconstruction still read as the cover rather than as a smudge.
//
// `src` is RGB (3 bytes per pixel), a `side`x`side` square starting at
// (ox, oy) inside a `src_w`-pixel-wide buffer; the crop is applied here
// instead of copying it out first, exactly as on the grey path. Cell
// bounds use the same integer arithmetic box_downsample uses, so the two
// squares describe the same cells -- a renderer reading both must never
// find them disagreeing about where a cell begins.
static std::vector<unsigned char> dominant_downsample(const unsigned char* src, int src_w,
                                                      int ox, int oy, int side, int target) {
    std::vector<unsigned char> out(static_cast<size_t>(target) * target * 3, 0);

    // Colour is quantised to 4 bits per channel (16 levels each, so
    // 16*16*16 = 4096 bins) before anything is counted, and the
    // quantisation is the whole trick. At full 8-bit precision a block of
    // a JPEG has almost no exact duplicates -- grain and DCT ringing give
    // nearly every pixel its own triplet -- so "the most common colour"
    // would degenerate into "some arbitrary pixel", i.e. noisy
    // nearest-neighbour. Bins 16 levels wide are comfortably wider than
    // that few-level noise, so pixels a human would call the same colour
    // land together and actually accumulate a count. Coarser (3 bits, 512
    // bins) starts merging colours that read as distinct -- red into
    // orange, navy into black -- and the cell stops resolving detail;
    // finer (5 bits, 32768 bins) re-fragments the counts and drifts back
    // toward nearest-neighbour. 4 bits is the widest binning that still
    // separates hues.
    constexpr int kBits = 4;
    constexpr int kShift = 8 - kBits;
    constexpr int kBinCount = 1 << (3 * kBits);

    // Sums are 64-bit because one cell can cover the whole source when
    // target is small: a 5000px cover at target=1 would overflow a 32-bit
    // accumulator at 255 per pixel.
    struct Bin { uint64_t count, r, g, b; };
    // Allocated once for the whole call rather than per cell. 4096 entries
    // is also far too many to clear wholesale per cell -- that would dwarf
    // the pixel work itself at small targets -- so `touched` records which
    // bins a cell actually used and only those get reset afterwards.
    std::vector<Bin> bins(kBinCount, Bin{0, 0, 0, 0});
    std::vector<int> touched;

    const size_t stride = static_cast<size_t>(src_w) * 3;

    for (int ty = 0; ty < target; ++ty) {
        int y0 = (ty * side) / target;
        int y1 = ((ty + 1) * side) / target;
        if (y0 >= side) y0 = side - 1;
        if (y1 <= y0) y1 = std::min(y0 + 1, side);

        for (int tx = 0; tx < target; ++tx) {
            int x0 = (tx * side) / target;
            int x1 = ((tx + 1) * side) / target;
            if (x0 >= side) x0 = side - 1;
            if (x1 <= x0) x1 = std::min(x0 + 1, side);

            unsigned char* dst = &out[(static_cast<size_t>(ty) * target + tx) * 3];

            // Fewer than two source pixels in the footprint means the
            // target is finer than the source, and there is no population
            // to take a mode of -- a "dominant colour" over one pixel is
            // just that pixel. Sample the pixel nearest the cell's centre
            // and move on; this degrades to pixel replication, which is
            // what box_downsample's clamped bounds already do on the grey
            // side.
            if ((y1 - y0) * (x1 - x0) < 2) {
                const int sy = std::min(((2 * ty + 1) * side) / (2 * target), side - 1);
                const int sx = std::min(((2 * tx + 1) * side) / (2 * target), side - 1);
                const unsigned char* p =
                    src + static_cast<size_t>(oy + sy) * stride +
                    static_cast<size_t>(ox + sx) * 3;
                dst[0] = p[0];
                dst[1] = p[1];
                dst[2] = p[2];
                continue;
            }

            touched.clear();
            for (int sy = y0; sy < y1; ++sy) {
                const unsigned char* row = src + static_cast<size_t>(oy + sy) * stride +
                                           static_cast<size_t>(ox) * 3;
                for (int sx = x0; sx < x1; ++sx) {
                    const unsigned char r = row[sx * 3 + 0];
                    const unsigned char g = row[sx * 3 + 1];
                    const unsigned char b = row[sx * 3 + 2];
                    const int idx = ((r >> kShift) << (2 * kBits)) |
                                    ((g >> kShift) << kBits) |
                                    (b >> kShift);
                    Bin& bin = bins[static_cast<size_t>(idx)];
                    if (bin.count == 0) touched.push_back(idx);
                    ++bin.count;
                    bin.r += r;
                    bin.g += g;
                    bin.b += b;
                }
            }

            int best = -1;
            uint64_t best_count = 0;
            uint64_t best_dist = 0;
            for (int idx : touched) {
                const Bin& bin = bins[static_cast<size_t>(idx)];
                // Tie-break: squared distance of the bin's own mean from
                // mid-grey. A cell that is half flat background and half
                // saturated detail splits its pixels evenly between two
                // bins, and which one wins decides whether the cell shows
                // the detail or the wash -- so ties go to whichever is
                // further from the mid-tone. Distance from grey rather
                // than a saturation ratio because deep shadow and specular
                // highlight are detail too, and both sit far out along the
                // cube's diagonal while the mush sits at its centre.
                const int64_t dr = static_cast<int64_t>(bin.r / bin.count) - 128;
                const int64_t dg = static_cast<int64_t>(bin.g / bin.count) - 128;
                const int64_t db = static_cast<int64_t>(bin.b / bin.count) - 128;
                const uint64_t dist = static_cast<uint64_t>(dr * dr + dg * dg + db * db);
                if (bin.count > best_count || (bin.count == best_count && dist > best_dist)) {
                    best = idx;
                    best_count = bin.count;
                    best_dist = dist;
                }
            }

            if (best >= 0) {
                // The winning bin's MEAN, not its centre. The bin centre
                // is only accurate to 16 levels per channel, which is
                // plainly visible as banding wherever the cover has a
                // large smooth area (a sky, a flat sleeve background):
                // neighbouring cells snap to the same lattice point and
                // the gradient turns into steps. Averaging the pixels that
                // actually landed in the bin emits a colour that really
                // occurs in the image, so only the *selection* is
                // quantised and the output stays smooth.
                const Bin& bin = bins[static_cast<size_t>(best)];
                dst[0] = static_cast<unsigned char>(bin.r / bin.count);
                dst[1] = static_cast<unsigned char>(bin.g / bin.count);
                dst[2] = static_cast<unsigned char>(bin.b / bin.count);
            }

            for (int idx : touched) bins[static_cast<size_t>(idx)] = Bin{0, 0, 0, 0};
        }
    }
    return out;
}

// Rescales whatever range the image actually occupies onto the full
// 0..255. Album art is very often low-contrast in grayscale terms (a dark
// photo, a washed-out pastel sleeve), and the terminal renderer has only
// a handful of distinguishable intensity steps to spend — without this,
// an image spanning 90..150 collapses into two or three glyphs and reads
// as a flat blob. Stretching first means those few steps are spread over
// the detail that is really there.
static void stretch_contrast(std::vector<unsigned char>& px) {
    if (px.empty()) return;
    auto mm = std::minmax_element(px.begin(), px.end());
    unsigned char lo = *mm.first;
    unsigned char hi = *mm.second;
    // A single flat tone (hi == lo) has no range to stretch — scaling it
    // would be a divide by zero, and there is nothing to reveal anyway.
    if (hi <= lo) return;
    const int span = static_cast<int>(hi) - static_cast<int>(lo);
    for (unsigned char& v : px) {
        v = static_cast<unsigned char>(((static_cast<int>(v) - lo) * 255) / span);
    }
}

// stb hands back a plain malloc'd buffer that nothing else owns, and the
// resampling between the load and the free can throw (allocation). A
// scope guard means the free happens on every path — normal return, early
// return, or unwind — instead of relying on one correctly-placed call.
namespace {
struct StbiBuffer {
    unsigned char* p = nullptr;
    ~StbiBuffer() { if (p) stbi_image_free(p); }
};
} // namespace

AlbumArt load_album_art(const fs::path& file, int target) {
    AlbumArt art;
    if (target <= 0 || file.empty()) return art;

    // Everything below this point is wrapped: this is called from the
    // render/loading path for whatever file happened to be on disk, and a
    // bad image must degrade to "no art" rather than take the player down.
    try {
        const std::string path = file.string();

        int w = 0, h = 0, n = 0;
        // req_comp=1 makes stb do the luminance conversion internally, so
        // we never have to care whether the source was gray, RGB or RGBA;
        // `n` reports the file's original channel count and is
        // intentionally unused. Note stb *drops* alpha rather than
        // compositing it, so a transparent PNG yields its RGB luminance
        // as if it were opaque — fine here, since cover art is opaque and
        // treating transparent regions as black would be the worse guess
        // for the odd PNG sleeve with a transparent border.
        StbiBuffer gbuf{stbi_load(path.c_str(), &w, &h, &n, 1)};
        if (!gbuf.p || w <= 0 || h <= 0) return art;

        // Center-crop to a square before resampling. Squashing a
        // non-square cover to fit would distort it, and cropping from a
        // corner would cut off the subject — sleeve artwork is centered
        // essentially without exception, so the center square is the
        // right box, and the renderer needs a square anyway (it spins the
        // bitmap about its own center).
        const int side = std::min(w, h);
        const int ox = (w - side) / 2;
        const int oy = (h - side) / 2;

        std::vector<unsigned char> px = box_downsample(gbuf.p, w, ox, oy, side, target);
        stretch_contrast(px);

        art.size = target;
        art.gray = std::move(px);

        // The colour square comes from a SECOND decode at req_comp=3,
        // rather than from one RGB decode with grey derived out of it, and
        // that is a deliberate trade. For an ordinary JFIF YCbCr JPEG,
        // asking stb for one channel makes it decode only the Y plane and
        // skip chroma entirely, so `gray` above is the file's own luma at
        // full precision. Deriving grey from a 3-channel decode instead
        // routes it through YCbCr -> clamped RGB -> stb's integer luma
        // weights ((77r + 150g + 29b) >> 8, i.e. not the same coefficients
        // the file was encoded with), which lands a byte or two off across
        // most of the image. `gray` feeds a renderer that is already tuned
        // and already correct, so the second decode buys byte-exact
        // preservation of it -- and it is paid on the cover-load path,
        // which is already dominated by disk and network, never per frame.
        int cw = 0, ch = 0, cn = 0;
        StbiBuffer cbuf{stbi_load(path.c_str(), &cw, &ch, &cn, 3)};
        // If the colour decode fails we still return the grey square: the
        // Braille and ASCII renderers are unaffected, and has_color() is a
        // separate predicate precisely so that a caller can find out. A
        // dimension mismatch between two decodes of one file by one
        // decoder should be impossible, but the crop offsets above are
        // only valid for (w, h) -- were it ever to happen, indexing the
        // colour buffer with them would run off the end of it.
        if (cbuf.p && cw == w && ch == h) {
            // Deliberately NOT contrast-stretched, unlike `gray`. The grey
            // path stretches because a 1-bit threshold has almost no range
            // to spend and needs the histogram pushed out to the full
            // 0..255 to resolve anything at all. Doing the same to colour
            // means rescaling each channel by its own factor, which
            // rotates hue -- a warm sleeve comes back green, a dark one
            // comes back lurid -- and blows out whatever was already
            // saturated. Recognisable in shape but wrong in colour is a
            // worse result here than slightly flat, and what the half-block
            // renderer is for is showing the cover's real colours.
            art.rgb = dominant_downsample(cbuf.p, w, ox, oy, side, target);
        }
        return art;
    } catch (...) {
        // Allocation failure on a pathological (huge) image, mostly.
        return AlbumArt{};
    }
}

// --- cache path --------------------------------------------------------

// Byte-for-byte the algorithm in CacheManager::sanitize. It is duplicated
// rather than shared because it is private to CacheManager and the art
// cache must produce *identical* slugs to the audio cache — an artwork
// file has to be findable from the same title that found the audio, so if
// these two ever disagree the art silently stops resolving. Keep the two
// in lockstep if either changes.
fs::path album_art_path(const std::string& title) {
    const char* home = std::getenv("HOME");
    fs::path base = home ? fs::path(home) : fs::path(".");
    fs::path dir = base / ".cache" / "mousiki" / "art";

    // Created here, the same way CacheManager's constructor does it, so
    // that the helper script (and any other writer) can just open the
    // returned path without each caller remembering to mkdir -p first.
    // Failure is ignored: it surfaces as a failed write/fetch instead.
    std::error_code ec;
    fs::create_directories(dir, ec);

    return dir / (sanitize_cache_name(title) + ".jpg");
}

// --- helper-script JSON ------------------------------------------------
// tiny_json.h (added upstream for snapshot.json) replaces what used to be
// three hand-rolled helpers here. Its string parser does not decode
// \uXXXX escapes, which is fine: fetch_art.py writes its line with
// ensure_ascii=False and surrogateescape, so a path arrives as raw bytes
// -- which is what the filesystem holds anyway. Only the script's
// last-ditch fallback (reached solely if that primary write already
// threw) can emit escapes, and a mangled path there simply reads as
// "no art for this track", which every caller already handles.

bool dominant_accent(const AlbumArt& art, unsigned char& out_r, unsigned char& out_g, unsigned char& out_b) {
    if (!art.has_color()) return false;
    // 4 bits per channel, the same bin width the per-cell downsample uses:
    // wide enough that JPEG grain lands in one bin, narrow enough that red
    // and orange stay apart.
    constexpr int kBins = 16 * 16 * 16;
    std::vector<double> score(kBins, 0.0);
    std::vector<double> sum_r(kBins, 0.0), sum_g(kBins, 0.0), sum_b(kBins, 0.0);
    std::vector<int> count(kBins, 0);

    const size_t n = static_cast<size_t>(art.size) * static_cast<size_t>(art.size);
    for (size_t i = 0; i < n; ++i) {
        const int r = art.rgb[i * 3], g = art.rgb[i * 3 + 1], b = art.rgb[i * 3 + 2];
        const int idx = ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4);
        const double mx = std::max({r, g, b}) / 255.0;
        const double mn = std::min({r, g, b}) / 255.0;
        const double sat = mx <= 0.0 ? 0.0 : (mx - mn) / mx;
        // Lightness window: pure black and pure white are common and useless
        // as an accent, so weight them down rather than excluding them
        // outright -- a genuinely monochrome cover should still yield
        // something rather than nothing.
        const double light = (mx + mn) / 2.0;
        const double light_w = (light < 0.12 || light > 0.92) ? 0.05 : 1.0;
        score[idx] += (0.15 + sat) * light_w;
        sum_r[idx] += r; sum_g[idx] += g; sum_b[idx] += b;
        ++count[idx];
    }
    int best = -1;
    double best_score = 0.0;
    for (int i = 0; i < kBins; ++i) {
        if (count[i] && score[i] > best_score) { best_score = score[i]; best = i; }
    }
    if (best < 0) return false;
    out_r = static_cast<unsigned char>(sum_r[best] / count[best] + 0.5);
    out_g = static_cast<unsigned char>(sum_g[best] / count[best] + 0.5);
    out_b = static_cast<unsigned char>(sum_b[best] / count[best] + 0.5);
    return true;
}

fs::path fetch_album_art(const fs::path& script_path, const std::string& title,
                         const std::string& artist) {
    if (script_path.empty()) return {};

    try {
        // Every interpolated value goes through shell_quote: run_capture
        // hands the string to `sh -c`, so an unquoted title containing a
        // quote, a space or a `;` would be reinterpreted as shell syntax.
        const std::string cmd = "python3 " + shell_quote(script_path.string()) +
                                " " + shell_quote(title) + " " + shell_quote(artist);
        ProcResult r = run_capture(cmd, /*merge_stderr=*/false);

        // Covers every failure shape at once — python3 missing
        // (exit_code < 0), the script crashing before it printed
        // anything, output that isn't JSON at all, and a well-formed
        // {"ok":false,...} response.
        tinyjson::Value root;
        if (r.out.empty() || !tinyjson::parse(r.out, root)) return {};

        const tinyjson::Value* ok = root.find("ok");
        if (!ok || !ok->as_bool(false)) return {};

        const tinyjson::Value* path = root.find("path");
        if (!path) return {};
        const std::string p = path->as_string();
        if (p.empty()) return {};
        return fs::path(p);
    } catch (...) {
        return {};
    }
}

} // namespace muisc
