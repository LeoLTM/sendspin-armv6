#include "display_layout.h"

#include <cstdio>

namespace {

// Marquee timing: hold at the start, then scroll left and wrap around.
constexpr uint32_t SCROLL_PAUSE_MS = 2500;
constexpr uint32_t SCROLL_PX_PER_S = 20;
constexpr int SCROLL_GAP_PX = 24;

/// Draw text in the box [x, x+w) at row y, scrolling it if it does not fit.
/// Returns true if the text is scrolling.
bool marquee(Framebuffer& fb, int x, int y, int w, const std::string& text, uint32_t scroll_ms) {
    int tw = Framebuffer::text_width(text);
    if (tw <= w) {
        fb.text(x, y, text);
        return false;
    }

    auto cycle_px = static_cast<uint32_t>(tw + SCROLL_GAP_PX);
    uint32_t period_ms = SCROLL_PAUSE_MS + cycle_px * 1000 / SCROLL_PX_PER_S;
    uint32_t t = scroll_ms % period_ms;
    int offset = t < SCROLL_PAUSE_MS
                     ? 0
                     : static_cast<int>((t - SCROLL_PAUSE_MS) * SCROLL_PX_PER_S / 1000);

    fb.text(x - offset, y, text, x, x + w);
    fb.text(x - offset + static_cast<int>(cycle_px), y, text, x, x + w);
    return true;
}

bool centered(Framebuffer& fb, int y, const std::string& text, uint32_t scroll_ms) {
    int tw = Framebuffer::text_width(text);
    if (tw >= fb.width()) return marquee(fb, 0, y, fb.width(), text, scroll_ms);
    fb.text((fb.width() - tw) / 2, y, text);
    return false;
}

void right_aligned(Framebuffer& fb, int y, const std::string& text) {
    fb.text(fb.width() - Framebuffer::text_width(text), y, text);
}

std::string format_time(uint32_t ms) {
    uint32_t s = ms / 1000;
    char buf[16];
    if (s >= 3600) {
        snprintf(buf, sizeof(buf), "%u:%02u:%02u", s / 3600, (s / 60) % 60, s % 60);
    } else {
        snprintf(buf, sizeof(buf), "%u:%02u", s / 60, s % 60);
    }
    return buf;
}

// 5x7 state icons, top-left at (x, y).
void icon_play(Framebuffer& fb, int x, int y) {
    for (int col = 0; col < 4; ++col) {
        for (int row = col; row < 7 - col; ++row) fb.set_pixel(x + col, y + row);
    }
}
void icon_pause(Framebuffer& fb, int x, int y) {
    fb.fill_rect(x, y, 2, 7);
    fb.fill_rect(x + 3, y, 2, 7);
}
void icon_stop(Framebuffer& fb, int x, int y) { fb.fill_rect(x, y + 1, 5, 5); }

enum class PlayState { CONNECTING, PAUSED, PLAYING, READY };

PlayState play_state(const DisplayState& s) {
    if (!s.connected) return PlayState::CONNECTING;
    if (s.paused) return PlayState::PAUSED;
    if (s.stream_active) return PlayState::PLAYING;
    return PlayState::READY;
}

void draw_state_icon(Framebuffer& fb, int x, int y, PlayState ps) {
    switch (ps) {
        case PlayState::PLAYING: icon_play(fb, x, y); break;
        case PlayState::PAUSED: icon_pause(fb, x, y); break;
        case PlayState::READY: icon_stop(fb, x, y); break;
        case PlayState::CONNECTING: break;
    }
}

/// Header line: state icon + label on the left, volume on the right.
void draw_header(Framebuffer& fb, int y, const DisplayState& s) {
    PlayState ps = play_state(s);
    const char* label = "Ready";
    switch (ps) {
        case PlayState::CONNECTING: label = "Connecting..."; break;
        case PlayState::PAUSED: label = "Paused"; break;
        case PlayState::PLAYING: label = "Playing"; break;
        case PlayState::READY: break;
    }
    if (ps == PlayState::CONNECTING) {
        fb.text(0, y, label);
    } else {
        draw_state_icon(fb, 0, y, ps);
        fb.text(8, y, label);
    }

    char vol[16];
    if (s.muted) {
        snprintf(vol, sizeof(vol), "Muted");
    } else {
        snprintf(vol, sizeof(vol), "Vol %u", s.volume);
    }
    right_aligned(fb, y, vol);
}

/// Progress bar in the box (x, y, w, h), filled proportionally.
void draw_progress_bar(Framebuffer& fb, int x, int y, int w, int h, uint32_t pos, uint32_t dur) {
    fb.rect(x, y, w, h);
    if (dur == 0) return;
    if (pos > dur) pos = dur;
    int inner_w = w - 4;
    int fill = static_cast<int>(static_cast<uint64_t>(inner_w) * pos / dur);
    fb.fill_rect(x + 2, y + 2, fill, h - 4);
}

/// Elapsed time on the left, total (or "LIVE") on the right.
void draw_times(Framebuffer& fb, int y, const DisplayState& s) {
    fb.text(0, y, format_time(s.progress_ms));
    right_aligned(fb, y, s.duration_ms > 0 ? format_time(s.duration_ms) : "LIVE");
}

// Rows are aligned to 8-pixel pages where possible so a scrolling line only
// dirties one page, keeping I2C traffic (and CPU use) low.

bool render_64(const DisplayState& s, uint32_t scroll_ms, Framebuffer& fb) {
    draw_header(fb, 0, s);
    fb.hline(0, fb.width() - 1, 10);

    if (s.title.empty()) {
        bool scrolling = centered(fb, 32, s.name, scroll_ms);
        if (!s.audio_format.empty()) centered(fb, 48, s.audio_format, scroll_ms);
        return scrolling;
    }

    bool scrolling = false;
    scrolling |= marquee(fb, 0, 16, fb.width(), s.title, scroll_ms);
    scrolling |= marquee(fb, 0, 24, fb.width(), s.artist, scroll_ms);
    scrolling |= marquee(fb, 0, 32, fb.width(), s.album, scroll_ms);
    fb.text(0, 40, s.audio_format);

    if (s.has_progress) {
        if (s.duration_ms > 0) {
            draw_progress_bar(fb, 0, 48, fb.width(), 6, s.progress_ms, s.duration_ms);
        }
        draw_times(fb, 56, s);
    }
    return scrolling;
}

bool render_32(const DisplayState& s, uint32_t scroll_ms, Framebuffer& fb) {
    if (s.title.empty()) {
        draw_header(fb, 0, s);
        return centered(fb, 16, s.name, scroll_ms);
    }

    bool scrolling = false;
    scrolling |= marquee(fb, 0, 0, fb.width(), s.title, scroll_ms);
    scrolling |= marquee(fb, 0, 8, fb.width(), s.artist, scroll_ms);

    if (s.has_progress) {
        if (s.duration_ms > 0) {
            draw_progress_bar(fb, 0, 17, fb.width(), 5, s.progress_ms, s.duration_ms);
        }
        draw_times(fb, 25, s);
    } else {
        scrolling |= marquee(fb, 0, 16, fb.width(), s.album, scroll_ms);
    }
    draw_state_icon(fb, (fb.width() - 5) / 2, 25, play_state(s));
    return scrolling;
}

}  // namespace

std::string format_audio_format(const std::string& codec, uint32_t sample_rate, uint8_t bit_depth) {
    std::string out = codec;
    char buf[16];
    if (sample_rate > 0) {
        // 48000 -> "48kHz", 44100 -> "44.1kHz", 88200 -> "88.2kHz"
        uint32_t tenths = (sample_rate + 50) / 100;
        if (tenths % 10 == 0) {
            snprintf(buf, sizeof(buf), "%ukHz", tenths / 10);
        } else {
            snprintf(buf, sizeof(buf), "%u.%ukHz", tenths / 10, tenths % 10);
        }
        if (!out.empty()) out += ' ';
        out += buf;
    }
    if (bit_depth > 0) {
        snprintf(buf, sizeof(buf), "%u-bit", bit_depth);
        if (!out.empty()) out += ' ';
        out += buf;
    }
    return out;
}

bool render_display(const DisplayState& state, uint32_t scroll_ms, Framebuffer& fb) {
    fb.clear();
    return fb.height() >= 64 ? render_64(state, scroll_ms, fb) : render_32(state, scroll_ms, fb);
}
