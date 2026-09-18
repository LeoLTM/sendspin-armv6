#pragma once

#include <cstdint>
#include <string>
#include <vector>

/// 1-bit framebuffer in the native SSD1306/SH1106 memory layout: the screen
/// is split into 8-pixel-tall "pages", and each byte is one column of a page
/// with bit 0 at the top.  This lets pages be sent to the panel verbatim.
class Framebuffer {
  public:
    Framebuffer(int width, int height);

    int width() const { return width_; }
    int height() const { return height_; }
    int pages() const { return height_ / 8; }

    /// Pointer to the width() bytes that make up one page.
    const uint8_t* page(int p) const { return &data_[static_cast<size_t>(p) * width_]; }

    void clear();
    void set_pixel(int x, int y);
    void hline(int x0, int x1, int y);
    void fill_rect(int x, int y, int w, int h);
    void rect(int x, int y, int w, int h);

    /// Draw printable-ASCII text with its top-left corner at (x, y).  Pixels
    /// outside [clip_x0, clip_x1) are dropped, which is what makes marquee
    /// scrolling work.  clip_x1 < 0 means the right edge of the screen.
    void text(int x, int y, const std::string& s, int clip_x0 = 0, int clip_x1 = -1);

    /// Pixel width of s when drawn with text() (no trailing spacing column).
    static int text_width(const std::string& s);

  private:
    int width_;
    int height_;
    std::vector<uint8_t> data_;
};

/// Convert UTF-8 to the printable ASCII the 5x7 font can draw.  Accented
/// Latin letters lose their accent (é -> e, ß -> ss), typographic quotes and
/// dashes become their ASCII look-alikes, and anything else becomes '?'.
std::string to_display_ascii(const std::string& utf8);
