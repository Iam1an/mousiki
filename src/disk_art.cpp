#include "disk_art.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace muisc {

namespace {

constexpr double PI = 3.14159265358979323846;

// Braille dot layout:
//   1 4
//   2 5
//   3 6
//   7 8
// Unicode Braille: U+2800 + dot bitmask

uint32_t decodeUTF8(const std::string& s, size_t& i) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) { ++i; return c; }
    if ((c & 0xE0) == 0xC0) {
        uint32_t r = ((c & 0x1F) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3F);
        i += 2; return r;
    }
    if ((c & 0xF0) == 0xE0) {
        uint32_t r = ((c & 0x0F) << 12) |
                     ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6) |
                     (static_cast<unsigned char>(s[i + 2]) & 0x3F);
        i += 3; return r;
    }
    uint32_t r = ((c & 0x07) << 18) |
                 ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 12) |
                 ((static_cast<unsigned char>(s[i + 2]) & 0x3F) << 6) |
                 (static_cast<unsigned char>(s[i + 3]) & 0x3F);
    i += 4; return r;
}

bool brailleDots(uint32_t cp, bool dots[4][2]) {
    if (cp < 0x2800 || cp > 0x28FF) return false;
    uint8_t bits = static_cast<uint8_t>(cp - 0x2800);
    dots[0][0] = bits & (1 << 0);
    dots[1][0] = bits & (1 << 1);
    dots[2][0] = bits & (1 << 2);
    dots[0][1] = bits & (1 << 3);
    dots[1][1] = bits & (1 << 4);
    dots[2][1] = bits & (1 << 5);
    dots[3][0] = bits & (1 << 6);
    dots[3][1] = bits & (1 << 7);
    return true;
}

std::string encodeBraille(bool dots[4][2]) {
    uint8_t bits = 0;
    if (dots[0][0]) bits |= 1 << 0;
    if (dots[1][0]) bits |= 1 << 1;
    if (dots[2][0]) bits |= 1 << 2;
    if (dots[0][1]) bits |= 1 << 3;
    if (dots[1][1]) bits |= 1 << 4;
    if (dots[2][1]) bits |= 1 << 5;
    if (dots[3][0]) bits |= 1 << 6;
    if (dots[3][1]) bits |= 1 << 7;

    uint32_t cp = 0x2800 + bits;
    std::string out;
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
    return out;
}

// The circular artwork from beta-ui.txt's disk rotation demo.
const char* kSourceArt =
"⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢀⣀⣠⣤⣤⣤⣤⣄⣀⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀\n"
"⠀⠀⠀⠀⠀⠀⠀⣠⣴⣾⣿⣿⣿⣿⣿⣿⣿⣿⣿⠃⠀⠀⢀⠀⠀⠀⠀⠀⠀⠀\n"
"⠀⠀⠀⠀⢀⣴⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡏⠀⠀⣠⣿⣿⣦⡀⠀⠀⠀⠀\n"
"⠀⠀⠀⣠⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠁⠀⣴⣿⣿⣿⣿⣿⣄⠀⠀⠀\n"
"⠀⠀⣰⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡿⠿⠿⢿⠃⢀⣾⣿⣿⣿⣿⣿⣿⣿⣆⠀⠀\n"
"⠀⢰⣿⣿⣿⣿⣿⣿⣿⣿⡿⠋⣡⣤⣶⣶⣤⣄⠘⢿⣿⣿⣿⣿⣿⣿⣿⣿⡆⠀\n"
"⠀⣾⣿⣿⣿⣿⣿⣿⣿⡿⠀⣾⣿⠟⠉⠉⠻⣿⣷⠀⢿⣿⣿⣿⣿⣿⣿⣿⣷⠀\n"
"⠀⣿⣿⣿⣿⣿⣿⣿⣿⡇⢸⣿⡇⠀⠀⠀⠀⢸⣿⡇⢸⣿⣿⣿⣿⣿⣿⣿⣿⠀\n"
"⠀⠿⠿⠟⠛⠛⢉⣉⣡⡤⠀⢿⣿⣤⣀⣀⣤⣿⡿⠀⣾⣿⣿⣿⣿⣿⣿⣿⡿⠀\n"
"⠀⠀⣤⣴⣶⡿⠟⢋⣡⣶⣶⣄⠙⠛⠿⠿⠛⠋⣠⣾⣿⣿⣿⣿⣿⣿⣿⣿⠇⠀\n"
"⠀⠀⠙⠋⢁⣠⣶⣿⣿⣿⣿⣿⣿⣷⣶⣶⣾⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠏⠀⠀\n"
"⠀⠀⠀⠰⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠏⠀⠀⠀\n"
"⠀⠀⠀⠀⠈⠻⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠟⠁⠀⠀⠀⠀\n"
"⠀⠀⠀⠀⠀⠀⠈⠙⠻⢿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡿⠟⠋⠁⠀⠀⠀⠀⠀⠀\n"
"⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠈⠉⠙⛛⛛⛛⛛⛛⠉⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀";

std::vector<std::string> split_lines(const std::string& source) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= source.size()) {
        size_t end = source.find('\n', start);
        if (end == std::string::npos) { lines.push_back(source.substr(start)); break; }
        lines.push_back(source.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

// The spindle hole, in dots. A real CD's hole is bare plastic, and punching it
// out is also what keeps the label from smearing into an unreadable blob at
// the very centre, where a whole turn's worth of image crowds into a handful
// of dots.
constexpr double kSpindleRadius = 4.0;

// Width, in dots, of the unlit ring that separates the label from the disc
// body. Without it a cover that inverts to mostly-lit dots (any dark cover)
// carries the same value as the solid disc around it and the label simply
// dissolves into the body. A real printed label never does that: it sits
// inside the disc's clear inner ring, and this is that ring.
constexpr double kLabelRimWidth = 1.6;

// Nearest-neighbour read from a dot bitmap; anything off the edge reads empty.
bool sampleDot(const std::vector<bool>& bmp, int w, int h, double x, double y) {
    const int ix = static_cast<int>(std::lround(x));
    const int iy = static_cast<int>(std::lround(y));
    if (ix < 0 || ix >= w || iy < 0 || iy >= h) return false;
    return bmp[static_cast<size_t>(iy) * static_cast<size_t>(w) + static_cast<size_t>(ix)];
}

// Pack a dot bitmap (cols*2 dots wide, rows*4 dots tall) back into Braille
// cells, using the same dot layout the decoder above reads.
std::vector<std::string> packBraille(const std::vector<bool>& bmp, int cols, int rows) {
    const int w = cols * 2;
    const int h = rows * 4;
    std::vector<std::string> out;
    out.reserve(static_cast<size_t>(rows));
    for (int row = 0; row < rows; ++row) {
        std::string line;
        for (int col = 0; col < cols; ++col) {
            bool dots[4][2]{};
            for (int dy = 0; dy < 4; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    const int x = col * 2 + dx;
                    const int y = row * 4 + dy;
                    if (x < w && y < h) {
                        dots[dy][dx] = bmp[static_cast<size_t>(y) * static_cast<size_t>(w) + x];
                    }
                }
            }
            line += encodeBraille(dots);
        }
        out.push_back(std::move(line));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Half-block colour rendering
//
// A Braille cell is 2x4 dots but carries only one colour, so the 1-bit
// renderer above has to throw the cover's colour away. Half-blocks trade dot
// resolution for colour: U+2580 ("▀") paints its top half in the cell's
// foreground colour and leaves the bottom half to the cell's background
// colour, so one text cell is two independently coloured pixels stacked
// vertically. Over the same 30x15 text area the disc already occupies that is
// a 30x30 colour image — coarser than 60x60 dots, but in colour.
// ---------------------------------------------------------------------------

// U+2580 UPPER HALF BLOCK and U+2584 LOWER HALF BLOCK. The lower one exists
// only so a cell whose *bottom* pixel is the coloured one can still be drawn
// with a foreground colour alone: setting a background instead would paint the
// empty top half too, and the panel behind the disc has to show through there.
const char* kUpperHalf = "▀";
const char* kLowerHalf = "▄";

// Reset after every coloured cell. The caller appends its own text to these
// lines, and an unreset background would run to the end of the terminal row.
const char* kReset = "\x1b[0m";

// One colour pixel: a colour, or nothing at all. "Nothing" is deliberately not
// black — it means no escape is emitted, so the panel behind the disc shows
// through. The spindle hole and everything outside the disc rim are nothing;
// a genuinely black cover pixel is a colour, and painting the two the same
// would turn the hole into a black dot and the disc into a black square.
struct ColorPixel {
    bool has = false;
    unsigned char r = 0, g = 0, b = 0;
};

// The six levels the xterm-256 colour cube quantises each channel to.
constexpr int kCubeLevels[6] = {0, 95, 135, 175, 215, 255};

int nearestCubeLevel(int v) {
    int best = 0;
    int best_d = v - kCubeLevels[0];
    if (best_d < 0) best_d = -best_d;
    for (int i = 1; i < 6; ++i) {
        int d = v - kCubeLevels[i];
        if (d < 0) d = -d;
        if (d < best_d) { best_d = d; best = i; }
    }
    return best;
}

long sqDist(int r1, int g1, int b1, int r2, int g2, int b2) {
    const long dr = r1 - r2, dg = g1 - g2, db = b1 - b2;
    return dr * dr + dg * dg + db * db;
}

// Map a 24-bit colour onto the nearest xterm-256 palette entry, considering
// BOTH halves of that palette: the 6x6x6 colour cube (16..231) and the 24-step
// greyscale ramp (232..255). Quantising to the cube alone is the obvious
// implementation and it is wrong for album art: the cube's grey diagonal has
// only six stops (0/95/135/175/215/255), so a near-grey cover — a monochrome
// photo, a washed-out pastel sleeve, anything desaturated — gets slammed onto
// those six and comes out blotchy and banded, with the banding crawling as the
// disc spins and pixels cross a stop. The ramp supplies 24 evenly spaced greys
// for exactly that case, so compute the best candidate from each half and keep
// whichever is genuinely closer in RGB distance; saturated colours still pick
// the cube, because no grey is near them.
int quantise256(int r, int g, int b) {
    const int ri = nearestCubeLevel(r);
    const int gi = nearestCubeLevel(g);
    const int bi = nearestCubeLevel(b);
    const long cube_err = sqDist(r, g, b, kCubeLevels[ri], kCubeLevels[gi], kCubeLevels[bi]);

    // The closest grey is the ramp step nearest the channel mean: minimising
    // (r-v)^2 + (g-v)^2 + (b-v)^2 over v puts v at the mean, so rounding the
    // mean onto the ramp's 8, 18, ... 238 stops is the exact answer, not an
    // approximation, and there is no need to scan all 24 entries.
    const double mean = (r + g + b) / 3.0;
    int step = static_cast<int>(std::lround((mean - 8.0) / 10.0));
    step = std::clamp(step, 0, 23);
    const int gv = 8 + 10 * step;
    const long grey_err = sqDist(r, g, b, gv, gv, gv);

    return grey_err < cube_err ? 232 + step : 16 + 36 * ri + 6 * gi + bi;
}

// Append one SGR colour escape: "\x1b[38;..." for foreground, "\x1b[48;..."
// for background. Zero printable width, which the cell-count invariant in
// frame_color() depends on.
void appendColorEscape(std::string& out, bool background, const ColorPixel& p, bool truecolor) {
    out += background ? "\x1b[48;" : "\x1b[38;";
    if (truecolor) {
        out += "2;";
        out += std::to_string(static_cast<int>(p.r));
        out += ';';
        out += std::to_string(static_cast<int>(p.g));
        out += ';';
        out += std::to_string(static_cast<int>(p.b));
    } else {
        out += "5;";
        out += std::to_string(quantise256(p.r, p.g, p.b));
    }
    out += 'm';
}

} // namespace

DiskArt::DiskArt() {
    std::vector<std::string> lines = split_lines(kSourceArt);

    height_ = static_cast<int>(lines.size());
    width_ = 0;
    for (const auto& line : lines) {
        int cells = 0;
        for (size_t i = 0; i < line.size();) { decodeUTF8(line, i); ++cells; }
        width_ = std::max(width_, cells);
    }

    // The old per-frame dot decode, done exactly once: the dots land in a
    // fixed source image that every frame samples, instead of being carried
    // around by the rotation.
    src_w_ = width_ * 2;
    src_h_ = height_ * 4;
    src_.assign(static_cast<size_t>(src_w_) * static_cast<size_t>(src_h_), false);

    for (int row = 0; row < height_; ++row) {
        int col = 0;
        for (size_t i = 0; i < lines[row].size();) {
            uint32_t cp = decodeUTF8(lines[row], i);
            bool b[4][2]{};
            // Non-Braille glyphs in the artwork (the U+26DB run on the last
            // line) decode to no dots at all, same as they always have.
            if (brailleDots(cp, b)) {
                for (int dy = 0; dy < 4; ++dy) {
                    for (int dx = 0; dx < 2; ++dx) {
                        if (!b[dy][dx]) continue;
                        const int x = col * 2 + dx;
                        const int y = row * 4 + dy;
                        if (x < src_w_ && y < src_h_) {
                            src_[static_cast<size_t>(y) * static_cast<size_t>(src_w_) + x] = true;
                        }
                    }
                }
            }
            ++col;
        }
    }
}

std::vector<std::string> DiskArt::frame(double angle) const {
    std::vector<bool> bitmap(static_cast<size_t>(src_w_) * static_cast<size_t>(src_h_), false);

    const double cx = (src_w_ - 1) / 2.0;
    const double cy = (src_h_ - 1) / 2.0;
    const double c = std::cos(angle);
    const double s = std::sin(angle);

    // Inverse mapping: walk every destination dot and rotate it back by
    // -angle to find the source dot it should show. The forward rotation was
    // [c -s; s c] (clockwise in terminal coords, where Y points down), so its
    // inverse is [c s; -s c] — that is cos(-angle)/sin(-angle) folded in by
    // hand rather than recomputing the trig. Spin direction is unchanged.
    for (int y = 0; y < src_h_; ++y) {
        for (int x = 0; x < src_w_; ++x) {
            const double dx = x - cx;
            const double dy = y - cy;
            const double sx = dx * c + dy * s;
            const double sy = -dx * s + dy * c;
            if (sampleDot(src_, src_w_, src_h_, sx + cx, sy + cy)) {
                bitmap[static_cast<size_t>(y) * static_cast<size_t>(src_w_) + x] = true;
            }
        }
    }

    return packBraille(bitmap, width_, height_);
}

std::vector<std::string> DiskArt::frame_with_label(double angle, const unsigned char* gray,
                                                   int gray_size, double label_radius,
                                                   double contrast, bool invert) const {
    std::vector<bool> bitmap(static_cast<size_t>(src_w_) * static_cast<size_t>(src_h_), false);

    const double cx = (src_w_ - 1) / 2.0;
    const double cy = (src_h_ - 1) / 2.0;
    const double c = std::cos(angle);
    const double s = std::sin(angle);

    const bool has_label = gray != nullptr && gray_size > 0 && label_radius > 0.0;
    // Cover pixels per Braille dot across the label's diameter.
    const double scale = has_label ? gray_size / (label_radius * 2.0) : 0.0;

    for (int y = 0; y < src_h_; ++y) {
        for (int x = 0; x < src_w_; ++x) {
            const double dx = x - cx;
            const double dy = y - cy;
            const double r = std::sqrt(dx * dx + dy * dy);
            if (r < kSpindleRadius) continue; // bare plastic — never lit

            // The same inverse rotation frame() uses, and it is applied to the
            // label as well as the disc art: both are sampled in the disc's
            // own un-rotated frame, which is what makes the label turn *with*
            // the disc instead of sitting still on top of a spinning one.
            const double sx = dx * c + dy * s;
            const double sy = -dx * s + dy * c;

            // See kLabelRimWidth: keep a bare ring just outside the label so
            // it always reads as a separate printed disc, whatever the cover's
            // overall tone happens to be.
            if (has_label && r > label_radius && r <= label_radius + kLabelRimWidth) continue;

            bool lit = false;
            if (has_label && r <= label_radius) {
                const int gx = static_cast<int>((sx + label_radius) * scale);
                const int gy = static_cast<int>((sy + label_radius) * scale);
                if (gx < 0 || gx >= gray_size || gy < 0 || gy >= gray_size) continue;
                double v = gray[static_cast<size_t>(gy) * static_cast<size_t>(gray_size) + gx] / 255.0;
                // Stretch around mid-grey, so a cover that is mostly one tone
                // still separates into ink and paper instead of going solid.
                v = std::clamp((v - 0.5) * contrast + 0.5, 0.0, 1.0);
                // Hard threshold, deliberately NOT an ordered dither. A
                // Braille cell is only 2x4 dots, so a dither matrix is coarser
                // than the cover features it is supposed to shade: the
                // mid-tones come out as crawling static rather than as grey,
                // and the crawl is worst while the disc spins, because the
                // pattern is fixed in destination space and the image moves
                // through it. One crisp bi-level edge per feature reads as
                // printing; a checkerboard reads as a broken terminal.
                lit = invert ? (v < 0.5) : (v > 0.5);
            } else {
                lit = sampleDot(src_, src_w_, src_h_, sx + cx, sy + cy);
            }

            if (lit) bitmap[static_cast<size_t>(y) * static_cast<size_t>(src_w_) + x] = true;
        }
    }

    return packBraille(bitmap, width_, height_);
}

std::vector<std::string> DiskArt::frame_color(double angle, const unsigned char* rgb,
                                              int rgb_size, double label_radius,
                                              bool truecolor) const {
    // The colour grid is width_ pixels across (one per text cell) and
    // height_*2 pixels down (two per cell) — 30x30 for this artwork.
    const int pw = width_;
    const int ph = height_ * 2;
    std::vector<ColorPixel> pixels(static_cast<size_t>(pw) * static_cast<size_t>(ph));

    // Centre and radii stay in DOT units, the same coordinate space frame()
    // and frame_with_label() work in, so the disc lands in the same place and
    // at the same size on screen whichever renderer drew it. A colour pixel
    // covers a 2x2 block of dots, so pixel x spans dot columns 2x and 2x+1 and
    // its centre sits at 2x + 0.5; those centres run 0.5 .. src_w_-1.5, whose
    // midpoint is exactly cx, so the colour grid is centred on the disc rather
    // than shifted half a pixel off it.
    const double cx = (src_w_ - 1) / 2.0;
    const double cy = (src_h_ - 1) / 2.0;
    const double c = std::cos(angle);
    const double s = std::sin(angle);

    const bool has_label = rgb != nullptr && rgb_size > 0 && label_radius > 0.0;
    // Cover pixels per dot across the label's diameter — the same scale
    // frame_with_label() uses, since the radius is in the same dot units.
    const double scale = has_label ? rgb_size / (label_radius * 2.0) : 0.0;

    for (int y = 0; y < ph; ++y) {
        for (int x = 0; x < pw; ++x) {
            const double dx = 2.0 * x + 0.5 - cx;
            const double dy = 2.0 * y + 0.5 - cy;
            const double r = std::sqrt(dx * dx + dy * dy);

            // Bare plastic at the centre, panel background outside the rim.
            // Both are "no colour" rather than a dark colour: see ColorPixel.
            // There is no kLabelRimWidth gap here — in colour the label *is*
            // the disc face, so there is no surrounding braille body for it to
            // be confused with, and a blank ring would just read as a chip out
            // of the artwork.
            if (r < kSpindleRadius) continue;
            if (!has_label || r > label_radius) continue;

            // The same inverse mapping frame() uses, for the same reason:
            // walking the destination and rotating each pixel back by -angle
            // gives every destination pixel an answer, where splatting the
            // source forward leaves holes. Forward rotation was [c -s; s c]
            // (clockwise with Y pointing down), so the inverse [c s; -s c] is
            // folded in by hand instead of recomputing the trig. Sampling the
            // cover in the disc's own un-rotated frame is also what makes the
            // artwork turn *with* the disc.
            const double sx = dx * c + dy * s;
            const double sy = -dx * s + dy * c;

            const int gx = static_cast<int>((sx + label_radius) * scale);
            const int gy = static_cast<int>((sy + label_radius) * scale);
            if (gx < 0 || gx >= rgb_size || gy < 0 || gy >= rgb_size) continue;

            // Nearest-neighbour, matching the dot renderer. No bilinear filter:
            // the grid is 30x30 for a whole cover, so smoothing only muddies
            // the few edges that survive the downsample.
            const size_t o = (static_cast<size_t>(gy) * static_cast<size_t>(rgb_size) +
                              static_cast<size_t>(gx)) * 3;
            ColorPixel& p = pixels[static_cast<size_t>(y) * static_cast<size_t>(pw) +
                                   static_cast<size_t>(x)];
            p.has = true;
            p.r = rgb[o];
            p.g = rgb[o + 1];
            p.b = rgb[o + 2];
        }
    }

    // INVARIANT: every line below contains exactly width_ glyph cells — one
    // per column, always, whether that cell is a half-block or a space. The
    // ANSI escapes are zero-width and must never be counted as cells, so the
    // caller's padding, centring and truncation arithmetic is identical to
    // what it does for frame()'s braille lines. Anything that would emit two
    // glyphs or none for a column breaks that contract.
    std::vector<std::string> out;
    out.reserve(static_cast<size_t>(height_));
    for (int row = 0; row < height_; ++row) {
        std::string line;
        for (int col = 0; col < width_; ++col) {
            const ColorPixel& top =
                pixels[static_cast<size_t>(row * 2) * static_cast<size_t>(pw) +
                       static_cast<size_t>(col)];
            const ColorPixel& bottom =
                pixels[static_cast<size_t>(row * 2 + 1) * static_cast<size_t>(pw) +
                       static_cast<size_t>(col)];

            if (top.has && bottom.has) {
                // Top half is the foreground of U+2580, bottom half is the
                // background behind it. One cell, two colours.
                appendColorEscape(line, false, top, truecolor);
                appendColorEscape(line, true, bottom, truecolor);
                line += kUpperHalf;
                line += kReset;
            } else if (top.has) {
                // Foreground only, no background set: the bottom half keeps
                // whatever the panel painted, which is what lets the disc's
                // curved edge sit on the background instead of inside a black
                // box drawn around it.
                appendColorEscape(line, false, top, truecolor);
                line += kUpperHalf;
                line += kReset;
            } else if (bottom.has) {
                // Mirror case: draw the lower half block in the foreground so
                // the *top* half is the one left transparent.
                appendColorEscape(line, false, bottom, truecolor);
                line += kLowerHalf;
                line += kReset;
            } else {
                line += ' ';
            }
        }
        out.push_back(std::move(line));
    }
    return out;
}

} // namespace muisc
