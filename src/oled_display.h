#pragma once

#include "display_layout.h"
#include "ssd1306.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

struct OledConfig {
    Ssd1306::Controller controller{Ssd1306::Controller::SSD1306};
    int i2c_bus{1};
    int i2c_address{0x3C};
    int height{64};
    bool rotate180{false};
    uint8_t contrast{128};
    int sleep_s{300};  // blank the panel after this long without playback; 0 = never
};

/// Now-playing OLED display driven from a low-priority background thread.
///
/// All setters are cheap and thread-safe: they only update shared state and
/// wake the render thread, so they are safe to call from Sendspin callbacks
/// without adding latency to audio processing.  I2C errors are logged and
/// the panel is re-initialised periodically, so a missing or flaky display
/// never affects playback.
class OledDisplay {
  public:
    OledDisplay(const OledConfig& config, const std::string& player_name);
    ~OledDisplay();

    OledDisplay(const OledDisplay&) = delete;
    OledDisplay& operator=(const OledDisplay&) = delete;

    void start();
    /// Blank the panel and join the render thread.  Idempotent.
    void stop();

    void set_connected(bool connected);
    void set_stream_active(bool active);
    void set_volume(uint8_t volume);
    void set_muted(bool muted);
    /// Strings are UTF-8 as received from the server.
    void set_track(const std::string& title, const std::string& artist, const std::string& album,
                   bool has_progress, bool paused);
    void clear_track();
    void set_progress(uint32_t progress_ms, uint32_t duration_ms);
    /// Current stream format, e.g. ("FLAC", 44100, 16).  Empty codec with
    /// zero rate and depth clears it.
    void set_audio_format(const std::string& codec, uint32_t sample_rate, uint8_t bit_depth);

  private:
    using clock = std::chrono::steady_clock;

    void thread_main();
    /// Apply a state change under the lock; wakes the thread if anything
    /// changed.  wake_panel resets the sleep timer.
    template <typename F> void update(F&& fn, bool wake_panel = true);

    OledConfig config_;

    std::mutex mutex_;
    std::condition_variable cv_;
    DisplayState state_;
    bool dirty_{true};
    bool stop_{false};
    clock::time_point last_activity_{clock::now()};
    clock::time_point text_changed_{clock::now()};

    std::thread thread_;
};
