#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace muisc {

namespace fs = std::filesystem;

// A square album-art bitmap carried in two forms: a single-channel
// grayscale copy, and a three-channel RGB copy of the same square.
//
// Both exist because the renderers disagree about what a pixel is. The
// Braille dot-cloud and the ASCII ramp collapse a pixel to one intensity
// and pick a glyph from it, so they want `gray` — contrast-stretched,
// which is the right call for a 1-bit threshold and the wrong one for
// colour (see load_album_art). The colored half-block renderer needs real
// per-pixel RGB, and no intensity buffer can reconstruct it: a red region
// and a blue region of equal luminance are the same byte in `gray`, so a
// cover drawn from intensity alone reads as grey mush. Carrying both is
// four bytes per pixel on a bitmap of at most a few thousand pixels —
// cheaper than re-decoding the file when the user switches renderer.
//
// Square because the rotating disk renderer spins the bitmap around its
// own center: a non-square source would sweep outside its own bounds and
// clip differently on every frame.
struct AlbumArt {
    int size = 0;                       // edge length in pixels; 0 = none
    std::vector<unsigned char> gray;    // size*size, row-major, 0=black 255=white
    std::vector<unsigned char> rgb;     // size*size*3, row-major, 8-bit RGB

    // Guards against the half-populated struct a failed decode returns.
    // Checks the buffer length too, not just size>0, so a caller can index
    // gray[y*size+x] over the full square without bounds-checking first.
    bool valid() const { return size > 0 && gray.size() == static_cast<size_t>(size) * size; }

    // The same contract for the color buffer, deliberately a separate
    // predicate rather than folded into valid(): every existing caller
    // asks valid() and then reads `gray`, and gating that on a buffer they
    // never touch would turn working art into "no art" for them. A color
    // renderer checks this one before indexing rgb[(y*size+x)*3 + c].
    bool has_color() const { return size > 0 && rgb.size() == static_cast<size_t>(size) * size * 3; }
};

// Decodes `file` (JPEG or PNG) and reduces it to a `target`-by-`target`
// square, filling `gray` and `rgb` both, so a caller never has to know
// which renderer will consume the result. Never throws and never reports
// why it failed — a missing, truncated or unsupported file is
// indistinguishable from "this track has no art" as far as the UI is
// concerned, and both just mean "draw the placeholder", so both come back
// as an invalid AlbumArt with both buffers empty.
// 64 is the default because it is comfortably above what a Braille grid
// can resolve at any realistic panel size, while still cheap to rotate.
AlbumArt load_album_art(const fs::path& file, int target = 64);

// Where a fetched cover for `title` is cached: $HOME/.cache/mousiki/art/
// <slug>.jpg. The slug is computed exactly the way CacheManager does it,
// so the art file sits beside its audio file under the same name and a
// cache hit can be checked without consulting any index. Title only, no
// artist — matching CacheManager, which keys the audio cache the same way.
fs::path album_art_path(const std::string& title);

// Runs the Python cover-art helper at `script_path` for (title, artist)
// and returns the path it downloaded to, or an empty path if it came back
// with "ok":false. Shelling out to Python rather than doing the HTTP here
// keeps this build free of an HTTP/TLS dependency, the same trade the
// lyrics fetcher makes. Callers should treat this as slow (network) and
// run it off the render thread.
fs::path fetch_album_art(const fs::path& script_path, const std::string& title,
                         const std::string& artist);

} // namespace muisc
