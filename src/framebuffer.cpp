#include "framebuffer.h"

#include "font5x7.h"

#include <algorithm>

Framebuffer::Framebuffer(int width, int height)
    : width_(width), height_(height), data_(static_cast<size_t>(width) * (height / 8), 0) {}

void Framebuffer::clear() { std::fill(data_.begin(), data_.end(), 0); }

void Framebuffer::set_pixel(int x, int y) {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) return;
    data_[static_cast<size_t>(y / 8) * width_ + x] |= static_cast<uint8_t>(1u << (y % 8));
}

void Framebuffer::hline(int x0, int x1, int y) {
    for (int x = x0; x <= x1; ++x) set_pixel(x, y);
}

void Framebuffer::fill_rect(int x, int y, int w, int h) {
    for (int yy = y; yy < y + h; ++yy) hline(x, x + w - 1, yy);
}

void Framebuffer::rect(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;
    hline(x, x + w - 1, y);
    hline(x, x + w - 1, y + h - 1);
    for (int yy = y; yy < y + h; ++yy) {
        set_pixel(x, yy);
        set_pixel(x + w - 1, yy);
    }
}

void Framebuffer::text(int x, int y, const std::string& s, int clip_x0, int clip_x1) {
    if (clip_x1 < 0) clip_x1 = width_;
    clip_x0 = std::max(clip_x0, 0);
    clip_x1 = std::min(clip_x1, width_);

    for (char c : s) {
        if (x >= clip_x1) break;
        if (x + font5x7::GLYPH_WIDTH > clip_x0) {
            if (c < font5x7::FIRST || c > font5x7::LAST) c = '?';
            const uint8_t* glyph = font5x7::GLYPHS[c - font5x7::FIRST];
            for (int col = 0; col < font5x7::GLYPH_WIDTH; ++col) {
                int px = x + col;
                if (px < clip_x0 || px >= clip_x1) continue;
                for (int row = 0; row < font5x7::HEIGHT; ++row) {
                    if (glyph[col] & (1u << row)) set_pixel(px, y + row);
                }
            }
        }
        x += font5x7::ADVANCE;
    }
}

int Framebuffer::text_width(const std::string& s) {
    if (s.empty()) return 0;
    return static_cast<int>(s.size()) * font5x7::ADVANCE - 1;
}

// ---------------------------------------------------------------------------
// UTF-8 -> ASCII folding
// ---------------------------------------------------------------------------

/// ASCII replacement for U+00C0..U+00FF (Latin-1 letters), indexed from 0xC0.
static const char* const LATIN1_FOLD[64] = {
    "A", "A", "A", "A", "A", "A", "AE", "C",  // C0-C7
    "E", "E", "E", "E", "I", "I", "I",  "I",  // C8-CF
    "D", "N", "O", "O", "O", "O", "O",  "x",  // D0-D7
    "O", "U", "U", "U", "U", "Y", "Th", "ss", // D8-DF
    "a", "a", "a", "a", "a", "a", "ae", "c",  // E0-E7
    "e", "e", "e", "e", "i", "i", "i",  "i",  // E8-EF
    "d", "n", "o", "o", "o", "o", "o",  "/",  // F0-F7
    "o", "u", "u", "u", "u", "y", "th", "y",  // F8-FF
};

static const char* fold_codepoint(uint32_t cp) {
    if (cp >= 0xC0 && cp <= 0xFF) return LATIN1_FOLD[cp - 0xC0];
    switch (cp) {
        case 0x00A0: return " ";    // no-break space
        case 0x2018:                // ‘
        case 0x2019:                // ’
        case 0x00B4: return "'";    // ´
        case 0x201C:                // “
        case 0x201D:                // ”
        case 0x00AB:                // «
        case 0x00BB: return "\"";   // »
        case 0x2010:                // hyphen
        case 0x2013:                // –
        case 0x2014: return "-";    // —
        case 0x2026: return "...";  // …
        case 0x0152: return "OE";   // Œ
        case 0x0153: return "oe";   // œ
        case 0x0160: return "S";    // Š
        case 0x0161: return "s";    // š
        case 0x017D: return "Z";    // Ž
        case 0x017E: return "z";    // ž
        default: return "?";
    }
}

std::string to_display_ascii(const std::string& utf8) {
    std::string out;
    out.reserve(utf8.size());

    size_t i = 0;
    while (i < utf8.size()) {
        auto b = static_cast<uint8_t>(utf8[i]);
        if (b < 0x80) {
            // Replace control characters (tabs, newlines) with a space.
            out += (b < 0x20 || b == 0x7F) ? ' ' : static_cast<char>(b);
            ++i;
            continue;
        }

        int extra = (b >= 0xF0) ? 3 : (b >= 0xE0) ? 2 : (b >= 0xC0) ? 1 : -1;
        if (extra < 0 || i + extra >= utf8.size()) {
            out += '?';  // stray continuation byte or truncated sequence
            ++i;
            continue;
        }

        uint32_t cp = b & (0x3F >> extra);
        bool valid = true;
        for (int k = 1; k <= extra; ++k) {
            auto cb = static_cast<uint8_t>(utf8[i + k]);
            if ((cb & 0xC0) != 0x80) { valid = false; break; }
            cp = (cp << 6) | (cb & 0x3F);
        }
        if (!valid) {
            out += '?';
            ++i;
            continue;
        }

        out += fold_codepoint(cp);
        i += extra + 1;
    }
    return out;
}
