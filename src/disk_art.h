#pragma once
#include <string>
#include <vector>

namespace muisc {

// Renders the rotating Braille-dot "disk" seen in the mockup. Started life as
// a direct port of beta-ui.txt's demo, which decoded the artwork into a cloud
// of dots and rotated that cloud forward once per frame. Forward rotation
// splats source dots *into* the destination, and rounding is not a bijection:
// two source dots can land on the same destination dot while a neighbouring
// destination dot is never written at all, so the disk came out lacy and
// moth-eaten while it span. It now decodes the artwork into a fixed source
// bitmap once, and each frame walks the *destination*, rotating every pixel
// back by -angle to ask which source dot belongs there. Every destination dot
// gets an answer, so the disk stays solid at any angle.
class DiskArt {
public:
    DiskArt();

    // Re-target the disc to `cells_wide` text cells across. Height follows at
    // half that, because a terminal cell is about twice as tall as it is
    // wide, which is what keeps the disc round. The hand-drawn source art
    // stays at its native 30x15 and is scaled into the new dot space, so the
    // plain disc still looks like itself; the art renderers gain real
    // resolution, which is the point -- at 30 cells an ASCII cover has only
    // 450 glyphs to work with.
    void resize(int cells_wide);

    // One rotated frame at `angle_radians`, as printable lines (no trailing
    // newlines). Always `height()` lines of `width()` Braille cells each.
    std::vector<std::string> frame(double angle_radians) const;

    // Same as frame(), but with `gray` (a square, row-major, 8-bit grayscale
    // image of gray_size x gray_size) rendered as the disc's printed label
    // inside `label_radius` dots of centre, rotating with the disc.
    // `contrast` is applied around mid-grey; `invert` maps DARK image areas to
    // lit braille dots, which is what makes a pale cover read as printed ink
    // rather than empty space. Pass gray == nullptr for the plain disc.
    std::vector<std::string> frame_with_label(double angle_radians,
                                              const unsigned char* gray, int gray_size,
                                              double label_radius = 15.0,
                                              double contrast = 1.7,
                                              bool invert = true) const;

    // Colour variant: draws `rgb` (a square, row-major, 8-bit RGB image of
    // rgb_size x rgb_size) across the disc face using half-block glyphs, two
    // vertical pixels per text cell, with the ANSI colour already embedded in
    // the returned strings. Rotates with the disc via the same inverse
    // mapping frame() uses. `truecolor` selects 24-bit colour; when false the
    // output is quantised to the xterm-256 cube for terminals without it.
    // Returns height() lines of width() cells, exactly like frame().
    std::vector<std::string> frame_color(double angle_radians,
                                         const unsigned char* rgb, int rgb_size,
                                         double label_radius = 29.0,
                                         bool truecolor = true) const;

    // Colour ASCII variant: one glyph per text cell, chosen from a density
    // ramp by the sampled colour's luma and printed in that colour. Half the
    // vertical resolution of frame_color's half-blocks (30x15 rather than
    // 30x30), but each cell carries a shape as well as a colour, which reads
    // as terminal art rather than as a photo mosaic. Same inverse rotation,
    // so it turns with the disc. `rgb` is rgb_size*rgb_size*3.
    // `glyph_mode` picks what the character means: 0 = density by brightness,
    // 1 = hue family (the colour already says how dark a cell is, so the glyph
    // is freed to say something else), 2 = one glyph everywhere.
    // `smoothing` in [0,1] is the per-frame blend factor toward each cell's
    // newly sampled colour: 1.0 snaps instantly (the old behaviour), lower
    // values ease. Rotation is the only thing that changes a cell's content
    // frame to frame, so easing is a mild temporal antialias -- it trades a
    // little smear for killing the single-frame glyph flips that read as
    // sparkle.
    std::vector<std::string> frame_ascii(double angle_radians,
                                         const unsigned char* rgb, int rgb_size,
                                         double label_radius = 29.0,
                                         bool truecolor = true,
                                         double smoothing = 1.0,
                                         int glyph_mode = 0) const;

    // Drop the eased per-cell state. Call when the cover changes, so a new
    // track's art doesn't cross-fade out of the previous one's.
    void reset_art_smoothing();

    int width() const { return width_; }
    int height() const { return height_; }

private:
    // The decoded artwork: one bool per Braille dot, row-major over
    // src_w_ x src_h_ (== width_*2 by height_*4). Built once in the ctor and
    // never rotated — the frame methods rotate their sampling coordinates
    // instead, which is the whole point of the inverse mapping.
    // The hand-drawn artwork at its native size, kept separate from `src_`
    // so resize() can rescale into the working bitmap without re-decoding.
    std::vector<bool> art_;
    int art_w_ = 0;
    int art_h_ = 0;
    // Dot-space size relative to the artwork's own 60x60, so radii quoted in
    // artwork units (the spindle hole, the caller's label_radius) still mean
    // the same fraction of the disc at any size.
    double scale_ = 1.0;

    // Eased per-cell colour, carried between frames for frame_ascii's
    // temporal smoothing. Mutable because the renderers are const and this
    // is a cache, not part of the disc's identity.
    mutable std::vector<float> smooth_rgb_;
    mutable bool smooth_valid_ = false;

    std::vector<bool> src_;
    int src_w_ = 0;
    int src_h_ = 0;
    int width_ = 0;
    int height_ = 0;
};

} // namespace muisc
