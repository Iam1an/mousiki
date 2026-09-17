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

} // namespace muisc
