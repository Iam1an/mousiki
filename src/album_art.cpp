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
        StbiBuffer buf{stbi_load(path.c_str(), &w, &h, &n, 1)};
        if (!buf.p || w <= 0 || h <= 0) return art;

        // Center-crop to a square before resampling. Squashing a
        // non-square cover to fit would distort it, and cropping from a
        // corner would cut off the subject — sleeve artwork is centered
        // essentially without exception, so the center square is the
        // right box, and the renderer needs a square anyway (it spins the
        // bitmap about its own center).
        const int side = std::min(w, h);
        const int ox = (w - side) / 2;
        const int oy = (h - side) / 2;

        std::vector<unsigned char> px = box_downsample(buf.p, w, ox, oy, side, target);
        stretch_contrast(px);

        art.size = target;
        art.gray = std::move(px);
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
