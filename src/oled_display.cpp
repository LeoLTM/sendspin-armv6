#include "oled_display.h"

#include <cstdio>
#include <cstring>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <vector>

namespace {

// Redraw rate while text is scrolling vs. when only the clock is ticking.
constexpr auto FRAME_INTERVAL_SCROLLING = std::chrono::milliseconds(100);
constexpr auto FRAME_INTERVAL_IDLE = std::chrono::milliseconds(1000);
// How often to retry opening a missing or failed panel.
constexpr auto RETRY_INTERVAL = std::chrono::seconds(10);

const char* controller_name(Ssd1306::Controller c) {
    return c == Ssd1306::Controller::SH1106 ? "SH1106" : "SSD1306";
}

}  // namespace

OledDisplay::OledDisplay(const OledConfig& config, const std::string& player_name)
    : config_(config) {
    state_.name = to_display_ascii(player_name);
}

OledDisplay::~OledDisplay() { stop(); }

void OledDisplay::start() {
    if (thread_.joinable()) return;
    thread_ = std::thread(&OledDisplay::thread_main, this);
}

void OledDisplay::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

template <typename F> void OledDisplay::update(F&& fn, bool wake_panel) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        DisplayState before = state_;
        fn(state_);
        if (state_ == before) return;
        dirty_ = true;
        if (wake_panel) last_activity_ = clock::now();
    }
    cv_.notify_all();
}

void OledDisplay::set_connected(bool connected) {
    update([&](DisplayState& s) { s.connected = connected; });
}

void OledDisplay::set_stream_active(bool active) {
    update([&](DisplayState& s) { s.stream_active = active; });
}

void OledDisplay::set_volume(uint8_t volume) {
    update([&](DisplayState& s) { s.volume = volume; });
}

void OledDisplay::set_muted(bool muted) {
    update([&](DisplayState& s) { s.muted = muted; });
}

void OledDisplay::set_track(const std::string& title, const std::string& artist,
                            const std::string& album, bool has_progress, bool paused) {
    std::string t = to_display_ascii(title);
    std::string ar = to_display_ascii(artist);
    std::string al = to_display_ascii(album);
    update([&](DisplayState& s) {
        if (s.title != t || s.artist != ar || s.album != al) {
            text_changed_ = clock::now();  // restart marquee from the beginning
        }
        s.title = std::move(t);
        s.artist = std::move(ar);
        s.album = std::move(al);
        s.has_progress = has_progress;
        s.paused = paused;
    });
}

void OledDisplay::clear_track() {
    update([&](DisplayState& s) {
        s.title.clear();
        s.artist.clear();
        s.album.clear();
        s.has_progress = false;
        s.paused = false;
        s.progress_ms = 0;
        s.duration_ms = 0;
    });
}

void OledDisplay::set_progress(uint32_t progress_ms, uint32_t duration_ms) {
    // Only whole seconds are shown, so drop the rest to avoid needless redraws.
    // Progress ticking along does not count as activity for the sleep timer.
    update(
        [&](DisplayState& s) {
            s.progress_ms = progress_ms / 1000 * 1000;
            s.duration_ms = duration_ms;
        },
        false);
}

void OledDisplay::set_audio_format(const std::string& codec, uint32_t sample_rate,
                                   uint8_t bit_depth) {
    std::string text = format_audio_format(codec, sample_rate, bit_depth);
    update([&](DisplayState& s) { s.audio_format = std::move(text); });
}

void OledDisplay::thread_main() {
    // Drawing is never urgent: run at the lowest priority so the render
    // thread can't steal CPU from audio decoding on the single-core Pi.
    setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), 19);

    Ssd1306 panel;
    Framebuffer fb(Ssd1306::WIDTH, config_.height);
    // Last contents sent per page, so only changed pages go over I2C.
    std::vector<std::vector<uint8_t>> sent(fb.pages());
    auto next_retry = clock::now();
    bool asleep = false;
    bool reported_failure = false;

    std::unique_lock<std::mutex> lock(mutex_);
    while (!stop_) {
        DisplayState s = state_;
        dirty_ = false;
        auto now = clock::now();
        bool playing = s.stream_active && !s.paused;
        if (playing) last_activity_ = now;
        bool sleep_due = config_.sleep_s > 0 && !playing &&
                         now - last_activity_ >= std::chrono::seconds(config_.sleep_s);
        auto scroll_ms = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now - text_changed_).count());
        lock.unlock();

        bool animating = false;

        if (!panel.is_open() && now >= next_retry) {
            if (panel.open(config_.i2c_bus, config_.i2c_address, config_.controller,
                           config_.height, config_.rotate180, config_.contrast)) {
                fprintf(stderr, "Display: %s 128x%d ready on /dev/i2c-%d at 0x%02X\n",
                        controller_name(config_.controller), config_.height, config_.i2c_bus,
                        config_.i2c_address);
                for (auto& page : sent) page.clear();
                asleep = false;
                reported_failure = false;
            } else {
                if (!reported_failure) {
                    fprintf(stderr, "Display: not available, retrying every %lld s "
                                    "(playback is unaffected)\n",
                            static_cast<long long>(RETRY_INTERVAL.count()));
                    reported_failure = true;
                }
                next_retry = now + RETRY_INTERVAL;
            }
        }

        if (panel.is_open()) {
            if (sleep_due) {
                if (!asleep) asleep = panel.set_display_on(false);
            } else {
                if (asleep && panel.set_display_on(true)) asleep = false;
                animating = render_display(s, scroll_ms, fb);
                for (int p = 0; p < fb.pages(); ++p) {
                    const uint8_t* data = fb.page(p);
                    if (sent[p].size() == Ssd1306::WIDTH &&
                        memcmp(sent[p].data(), data, Ssd1306::WIDTH) == 0) {
                        continue;
                    }
                    if (!panel.write_page(p, data)) {
                        fprintf(stderr, "Display: I2C write failed, will reinitialise\n");
                        panel.close();
                        next_retry = now + RETRY_INTERVAL;
                        break;
                    }
                    sent[p].assign(data, data + Ssd1306::WIDTH);
                }
            }
        }

        auto wait = (panel.is_open() && !asleep && animating) ? FRAME_INTERVAL_SCROLLING
                                                              : FRAME_INTERVAL_IDLE;
        lock.lock();
        cv_.wait_for(lock, wait, [this] { return stop_ || dirty_; });
    }
    lock.unlock();

    panel.close();  // switches the panel off
}
