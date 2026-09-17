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

    int width() const { return width_; }
    int height() const { return height_; }

private:
    // The decoded artwork: one bool per Braille dot, row-major over
    // src_w_ x src_h_ (== width_*2 by height_*4). Built once in the ctor and
    // never rotated — the frame methods rotate their sampling coordinates
    // instead, which is the whole point of the inverse mapping.
    std::vector<bool> src_;
    int src_w_ = 0;
    int src_h_ = 0;
    int width_ = 0;
    int height_ = 0;
};

} // namespace muisc
