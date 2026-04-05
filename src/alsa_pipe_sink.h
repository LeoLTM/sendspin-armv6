#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>

/// Audio sink that pipes decoded PCM to aplay (ALSA).
/// Avoids cross-compiling PortAudio — aplay ships with Raspberry Pi OS.
class AlsaPipeSink {
  public:
    /// ALSA hardware buffer duration passed to aplay via --buffer-time, in
    /// microseconds.  Pinning this to a fixed value makes the pipeline latency
    /// predictable.  Set PlayerRole::Config::fixed_delay_us to this constant
    /// so the sync task compensates for the full aplay output latency.
    static constexpr int32_t ALSA_BUFFER_TIME_US = 90000;

    AlsaPipeSink() = default;
    ~AlsaPipeSink();

    AlsaPipeSink(const AlsaPipeSink&) = delete;
    AlsaPipeSink& operator=(const AlsaPipeSink&) = delete;

    /// Configure and (re)open the aplay pipe for the given format.
    bool configure(uint32_t sample_rate, uint8_t channels, uint8_t bits_per_sample);

    /// Write PCM data to the aplay pipe.  Returns bytes written.
    size_t write(uint8_t* data, size_t length, uint32_t timeout_ms);

    /// Stop playback and close the pipe.
    void stop();

    /// Discard current stream (close + reopen is handled by on_stream_start).
    void clear();

    /// Set volume (0–100). Applied as software scaling before writing.
    void set_volume(uint8_t volume);

    /// Set mute state.
    void set_muted(bool muted);

    /// Set ALSA device name (e.g. "plughw:1,0"). Must be called before the
    /// first configure(). Empty string uses the system default device.
    void set_device(const std::string& device);

    /// Callback for timing feedback — wired to player.notify_audio_played().
    std::function<void(uint32_t frames, int64_t timestamp)> on_frames_played;

  private:
    FILE* pipe_{nullptr};
    std::string device_;
    uint32_t sample_rate_{0};
    uint8_t channels_{0};
    uint8_t bits_per_sample_{0};
    size_t bytes_per_frame_{0};

    // Running total of bytes written to the aplay pipe but not yet played.
    // Used to compute the output timestamp passed to notify_audio_played().
    size_t buffered_bytes_{0};

    uint8_t volume_{100};
    bool muted_{false};

    void apply_volume(uint8_t* data, size_t length);
};
