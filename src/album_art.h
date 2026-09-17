#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace muisc {

namespace fs = std::filesystem;

// A square, single-channel grayscale bitmap — the only form of album art
// the terminal renderer can actually use. Color is dropped at decode time
// rather than here because every consumer (Braille dot-cloud, ASCII ramp,
// half-block shading) ultimately picks a glyph from one intensity value,
// so carrying RGB through would be three times the memory for data that
// gets collapsed anyway. Square because the rotating disk renderer spins
// the bitmap around its own center: a non-square source would sweep
// outside its own bounds and clip differently on every frame.
struct AlbumArt {
    int size = 0;                       // edge length in pixels; 0 = none
    std::vector<unsigned char> gray;    // size*size, row-major, 0=black 255=white

    // Guards against the half-populated struct a failed decode returns.
    // Checks the buffer length too, not just size>0, so a caller can index
    // gray[y*size+x] over the full square without bounds-checking first.
    bool valid() const { return size > 0 && gray.size() == static_cast<size_t>(size) * size; }
};

// Decodes `file` (JPEG or PNG) and reduces it to a `target`-by-`target`
// grayscale square. Never throws and never reports why it failed — a
// missing, truncated or unsupported file is indistinguishable from "this
// track has no art" as far as the UI is concerned, and both just mean
// "draw the placeholder", so both come back as an invalid AlbumArt.
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
