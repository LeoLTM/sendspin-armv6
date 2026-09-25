#pragma once

#include "framebuffer.h"

#include <cstdint>
#include <string>

/// Everything the OLED shows, as plain data.  Strings are already folded to
/// printable ASCII (see to_display_ascii()).
struct DisplayState {
    std::string name;       // player name, shown when nothing is playing
    bool connected{false};  // connected to the Sendspin server
    bool stream_active{false};
    bool paused{false};     // server reports playback_speed == 0
    uint8_t volume{100};
    bool muted{false};

    std::string title;
    std::string artist;
    std::string album;
    bool has_progress{false};
    uint32_t progress_ms{0};
    uint32_t duration_ms{0};  // 0 = unknown / live stream

    std::string audio_format;  // e.g. "FLAC 44.1kHz 16-bit"; empty when no stream

    bool operator==(const DisplayState&) const = default;
};

/// Format stream parameters for display, e.g. ("FLAC", 44100, 16) ->
/// "FLAC 44.1kHz 16-bit".  A zero rate or bit depth is left out.
std::string format_audio_format(const std::string& codec, uint32_t sample_rate, uint8_t bit_depth);

/// Draw the now-playing screen into fb (128x64 or 128x32).
///
/// scroll_ms is the time since the track text last changed; lines too long
/// for the screen scroll as a marquee driven by it.
///
/// Returns true if any line is scrolling, so the caller should keep redrawing
/// at animation speed rather than only on state changes.
bool render_display(const DisplayState& state, uint32_t scroll_ms, Framebuffer& fb);
