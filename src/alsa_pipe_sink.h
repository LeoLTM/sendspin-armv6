#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

/// Audio sink using the ALSA API directly with a dedicated playback thread.
///
/// Replaces the previous aplay-pipe approach to enable:
///  - snd_pcm_delay() queries for hardware-accurate timing feedback
///  - Explicit ALSA hwparams (period / buffer size) for granular sync
///  - Decoupled playback thread so audio output never blocks network I/O
class AlsaPipeSink {
  public:
    /// Pipeline delay declared to the sync task.  With direct ALSA and
    /// snd_pcm_delay()-based timestamps the reported playback time already
    /// accounts for the hardware buffer — no extra compensation is needed.
    static constexpr int32_t PIPELINE_DELAY_US = 0;

    AlsaPipeSink() = default;
    ~AlsaPipeSink();

    AlsaPipeSink(const AlsaPipeSink&) = delete;
    AlsaPipeSink& operator=(const AlsaPipeSink&) = delete;

    /// Configure and (re)open the ALSA device for the given format.
    bool configure(uint32_t sample_rate, uint8_t channels, uint8_t bits_per_sample);

    /// Write PCM data into the ring buffer.  Returns bytes accepted.
    size_t write(uint8_t* data, size_t length, uint32_t timeout_ms);

    /// Stop playback, close the device, and join the playback thread.
    void stop();

    /// Flush ring buffer and ALSA queue without closing the device.
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
    // ---- ALSA (handle stored as void* to keep <alsa/asoundlib.h> out) ----
    void* pcm_{nullptr};
    std::string device_;
    uint32_t sample_rate_{0};
    uint8_t channels_{0};
    uint8_t bits_per_sample_{0};
    size_t bytes_per_frame_{0};
    unsigned long period_frames_{0};
    unsigned long buffer_frames_{0};

    bool open_alsa();
    void close_alsa();
    bool recover_xrun(int err);

    // ---- Ring buffer (decouples sendspin thread from playback thread) ----
    static constexpr size_t RING_CAPACITY = 2 * 1024 * 1024;  // 2 MiB
    std::vector<uint8_t> ring_;
    size_t ring_head_{0};
    size_t ring_tail_{0};
    size_t ring_used_{0};
    std::mutex ring_mtx_;
    std::condition_variable ring_cv_;

    // ---- Playback thread ----
    std::thread playback_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> flush_requested_{false};

    void playback_loop();

    // ---- Volume ----
    uint8_t volume_{100};
    bool muted_{false};

    void apply_volume(uint8_t* data, size_t length);
};
