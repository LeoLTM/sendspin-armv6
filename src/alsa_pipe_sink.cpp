#include "alsa_pipe_sink.h"

#include <alsa/asoundlib.h>
#include <pthread.h>
#include <sched.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>

// pcm_ is stored as void* so the header does not depend on <alsa/asoundlib.h>.
#define PCM static_cast<snd_pcm_t*>(pcm_)

// ---------------------------------------------------------------------------
// Device name validation
// ---------------------------------------------------------------------------

AlsaPipeSink::~AlsaPipeSink() { stop(); }

void AlsaPipeSink::set_device(const std::string& device) {
    for (unsigned char c : device) {
        if (!std::isalnum(c) && c != ':' && c != ',' && c != '_' &&
            c != '-' && c != '.' && c != '=') {
            fprintf(stderr,
                    "AlsaPipeSink: invalid character '%c' in device name — "
                    "ignoring device setting\n",
                    c);
            return;
        }
    }
    device_ = device;
}

// ---------------------------------------------------------------------------
// ALSA lifecycle
// ---------------------------------------------------------------------------

bool AlsaPipeSink::open_alsa() {
    const char* dev = device_.empty() ? "default" : device_.c_str();

    snd_pcm_t* handle = nullptr;
    int err = snd_pcm_open(&handle, dev, SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        fprintf(stderr, "AlsaPipeSink: cannot open '%s': %s\n",
                dev, snd_strerror(err));
        return false;
    }
    pcm_ = handle;

    // ---- Hardware parameters ----
    snd_pcm_hw_params_t* hw;
    snd_pcm_hw_params_alloca(&hw);
    snd_pcm_hw_params_any(PCM, hw);

    snd_pcm_hw_params_set_access(PCM, hw, SND_PCM_ACCESS_RW_INTERLEAVED);

    snd_pcm_format_t fmt;
    switch (bits_per_sample_) {
        case 16: fmt = SND_PCM_FORMAT_S16_LE; break;
        case 24: fmt = SND_PCM_FORMAT_S24_3LE; break;
        case 32: fmt = SND_PCM_FORMAT_S32_LE; break;
        default:
            fprintf(stderr, "AlsaPipeSink: unsupported bit depth %u\n",
                    bits_per_sample_);
            close_alsa();
            return false;
    }
    snd_pcm_hw_params_set_format(PCM, hw, fmt);
    snd_pcm_hw_params_set_channels(PCM, hw, channels_);
    snd_pcm_hw_params_set_rate(PCM, hw, sample_rate_, 0);

    // Request tight buffer / period sizes for granular sync feedback.
    // 100 ms buffer absorbs scheduling jitter on the single-core Pi Zero;
    // 20 ms periods give the sync algorithm timely updates without
    // overwhelming the CPU with interrupts.
    unsigned int buf_time = 100000;  // 100 ms
    unsigned int per_time =  20000;  //  20 ms
    int dir = 0;
    snd_pcm_hw_params_set_buffer_time_near(PCM, hw, &buf_time, &dir);
    dir = 0;
    snd_pcm_hw_params_set_period_time_near(PCM, hw, &per_time, &dir);

    err = snd_pcm_hw_params(PCM, hw);
    if (err < 0) {
        fprintf(stderr, "AlsaPipeSink: hw_params failed: %s\n",
                snd_strerror(err));
        close_alsa();
        return false;
    }

    // Read back negotiated values.
    snd_pcm_uframes_t pf = 0, bf = 0;
    snd_pcm_hw_params_get_period_size(hw, &pf, &dir);
    snd_pcm_hw_params_get_buffer_size(hw, &bf);
    period_frames_ = static_cast<unsigned long>(pf);
    buffer_frames_ = static_cast<unsigned long>(bf);

    // ---- Software parameters ----
    snd_pcm_sw_params_t* sw;
    snd_pcm_sw_params_alloca(&sw);
    snd_pcm_sw_params_current(PCM, sw);
    // Start hardware output after two periods are queued.
    snd_pcm_sw_params_set_start_threshold(PCM, sw, pf * 2);
    snd_pcm_sw_params_set_avail_min(PCM, sw, pf);
    snd_pcm_sw_params(PCM, sw);

    fprintf(stderr,
            "AlsaPipeSink: %s — %u Hz %u ch %u bit, "
            "period %lu frames, buffer %lu frames\n",
            dev, sample_rate_, channels_, bits_per_sample_,
            period_frames_, buffer_frames_);
    return true;
}

void AlsaPipeSink::close_alsa() {
    if (pcm_) {
        snd_pcm_close(PCM);
        pcm_ = nullptr;
    }
}

bool AlsaPipeSink::recover_xrun(int err) {
    if (err == -EPIPE) {
        fprintf(stderr, "AlsaPipeSink: underrun, recovering\n");
        return snd_pcm_prepare(PCM) >= 0;
    }
    if (err == -ESTRPIPE) {
        int rc;
        while ((rc = snd_pcm_resume(PCM)) == -EAGAIN)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (rc < 0) rc = snd_pcm_prepare(PCM);
        return rc >= 0;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Stream lifecycle
// ---------------------------------------------------------------------------

bool AlsaPipeSink::configure(uint32_t sample_rate, uint8_t channels,
                             uint8_t bits_per_sample) {
    stop();

    sample_rate_ = sample_rate;
    channels_ = channels;
    bits_per_sample_ = bits_per_sample;
    bytes_per_frame_ = channels * (bits_per_sample / 8);

    ring_.resize(RING_CAPACITY);
    ring_head_ = ring_tail_ = ring_used_ = 0;
    flush_requested_.store(false);

    if (!open_alsa()) return false;

    running_.store(true);
    playback_thread_ = std::thread([this] { playback_loop(); });
    return true;
}

void AlsaPipeSink::stop() {
    if (running_.exchange(false)) {
        // snd_pcm_drop() unblocks any pending snd_pcm_writei() in the
        // playback thread so it observes running_ == false and exits.
        if (pcm_) snd_pcm_drop(PCM);
        ring_cv_.notify_all();
        if (playback_thread_.joinable()) playback_thread_.join();
    }
    close_alsa();
    {
        std::lock_guard<std::mutex> lk(ring_mtx_);
        ring_head_ = ring_tail_ = ring_used_ = 0;
    }
}

void AlsaPipeSink::clear() {
    {
        std::lock_guard<std::mutex> lk(ring_mtx_);
        ring_head_ = ring_tail_ = ring_used_ = 0;
    }
    flush_requested_.store(true);
    ring_cv_.notify_all();
}

// ---------------------------------------------------------------------------
// Volume
// ---------------------------------------------------------------------------

void AlsaPipeSink::set_volume(uint8_t volume) {
    volume_.store(volume > 100 ? 100 : volume);
}

void AlsaPipeSink::set_muted(bool muted) { muted_.store(muted); }

void AlsaPipeSink::apply_volume(uint8_t* data, size_t length) {
    const uint8_t vol = volume_.load();
    const bool muted  = muted_.load();
    if (!muted && vol >= 100) return;

    if (muted || vol == 0) {
        std::memset(data, 0, length);
        return;
    }

    // Quadratic curve for perceptually uniform volume control.
    // scale = (vol/100)^2 in Q32 fixed-point.
    static constexpr uint64_t Q32_ONE = UINT64_C(1) << 32;
    static constexpr int FRAC_BITS = 32;
    static constexpr int64_t ROUND = INT64_C(1) << (FRAC_BITS - 1);

    uint64_t v = vol;
    int64_t scale = static_cast<int64_t>((v * v * Q32_ONE) / 10000);

    uint8_t bps = bits_per_sample_ / 8;
    switch (bps) {
        case 2: {
            size_t count = length / 2;
            auto* samples = reinterpret_cast<int16_t*>(data);
            for (size_t i = 0; i < count; ++i) {
                samples[i] = static_cast<int16_t>(
                    (static_cast<int64_t>(samples[i]) * scale + ROUND) >> FRAC_BITS);
            }
            break;
        }
        case 3: {
            size_t count = length / 3;
            for (size_t i = 0; i < count; ++i) {
                uint8_t* p = data + i * 3;
                int32_t sample = static_cast<int32_t>(
                    p[0] | (p[1] << 8) | (p[2] << 16));
                if (sample & 0x800000) sample |= static_cast<int32_t>(0xFF000000);
                int32_t out = static_cast<int32_t>(
                    (static_cast<int64_t>(sample) * scale + ROUND) >> FRAC_BITS);
                p[0] = static_cast<uint8_t>(out & 0xFF);
                p[1] = static_cast<uint8_t>((out >> 8) & 0xFF);
                p[2] = static_cast<uint8_t>((out >> 16) & 0xFF);
            }
            break;
        }
        case 4: {
            size_t count = length / 4;
            auto* samples = reinterpret_cast<int32_t*>(data);
            for (size_t i = 0; i < count; ++i) {
                samples[i] = static_cast<int32_t>(
                    (static_cast<int64_t>(samples[i]) * scale + ROUND) >> FRAC_BITS);
            }
            break;
        }
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Ring buffer producer (called from sendspin's network/decode thread)
// ---------------------------------------------------------------------------

size_t AlsaPipeSink::write(uint8_t* data, size_t length,
                           uint32_t timeout_ms) {
    if (!running_.load()) return length;  // discard if not running

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);
    size_t written = 0;

    std::unique_lock<std::mutex> lk(ring_mtx_);
    while (written < length && running_.load()) {
        size_t space = RING_CAPACITY - ring_used_;
        if (space == 0) {
            if (std::chrono::steady_clock::now() >= deadline) break;
            ring_cv_.wait_until(lk, deadline, [&] {
                return (RING_CAPACITY - ring_used_) > 0 || !running_.load();
            });
            continue;
        }

        size_t chunk = std::min(length - written, space);
        // Copy into the ring, handling wrap-around.
        size_t head_room = RING_CAPACITY - ring_head_;
        size_t first = std::min(chunk, head_room);
        std::memcpy(ring_.data() + ring_head_, data + written, first);
        if (chunk > first)
            std::memcpy(ring_.data(), data + written + first, chunk - first);
        ring_head_ = (ring_head_ + chunk) % RING_CAPACITY;
        ring_used_ += chunk;
        written += chunk;
    }
    lk.unlock();
    ring_cv_.notify_one();  // wake playback thread

    return written;
}

// ---------------------------------------------------------------------------
// Ring buffer consumer / ALSA writer (dedicated playback thread)
// ---------------------------------------------------------------------------

void AlsaPipeSink::playback_loop() {
    // Elevate this thread to SCHED_FIFO so ALSA writes are not delayed by
    // network I/O sharing the single core.  Silently ignored if we lack
    // CAP_SYS_NICE.
    {
        struct sched_param sp = {};
        sp.sched_priority = 1;  // lowest RT priority
        pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
    }

    const size_t period_bytes =
        static_cast<size_t>(period_frames_) * bytes_per_frame_;
    std::vector<uint8_t> buf(period_bytes);

    while (running_.load()) {
        // ---- Handle flush request (from clear()) ----
        if (flush_requested_.exchange(false)) {
            snd_pcm_drop(PCM);
            snd_pcm_prepare(PCM);
            continue;
        }

        // ---- Read from ring buffer ----
        size_t to_read = 0;
        {
            std::unique_lock<std::mutex> lk(ring_mtx_);
            if (ring_used_ < bytes_per_frame_) {
                ring_cv_.wait_for(lk, std::chrono::milliseconds(5), [&] {
                    return ring_used_ >= bytes_per_frame_ || !running_.load()
                           || flush_requested_.load();
                });
                if (!running_.load() || flush_requested_.load()) continue;
                if (ring_used_ < bytes_per_frame_) continue;
            }

            to_read = std::min(ring_used_, period_bytes);
            to_read = (to_read / bytes_per_frame_) * bytes_per_frame_;

            size_t tail_room = RING_CAPACITY - ring_tail_;
            size_t first = std::min(to_read, tail_room);
            std::memcpy(buf.data(), ring_.data() + ring_tail_, first);
            if (to_read > first)
                std::memcpy(buf.data() + first, ring_.data(),
                            to_read - first);
            ring_tail_ = (ring_tail_ + to_read) % RING_CAPACITY;
            ring_used_ -= to_read;
        }
        ring_cv_.notify_one();  // wake producer if it was blocked

        // ---- Apply volume (done here so changes take effect immediately) ----
        apply_volume(buf.data(), to_read);

        // ---- Write to ALSA (blocking — provides natural pacing) ----
        snd_pcm_uframes_t frames =
            static_cast<snd_pcm_uframes_t>(to_read / bytes_per_frame_);
        snd_pcm_uframes_t offset = 0;

        while (offset < frames && running_.load()) {
            snd_pcm_sframes_t n = snd_pcm_writei(
                PCM, buf.data() + offset * bytes_per_frame_, frames - offset);
            if (n < 0) {
                if (!running_.load()) break;
                if (recover_xrun(static_cast<int>(n))) continue;
                fprintf(stderr, "AlsaPipeSink: write error: %s\n",
                        snd_strerror(static_cast<int>(n)));
                running_.store(false);
                break;
            }
            offset += static_cast<snd_pcm_uframes_t>(n);
        }

        // ---- Timing feedback via snd_pcm_delay() ----
        if (offset > 0 && on_frames_played) {
            snd_pcm_sframes_t delay = 0;
            if (snd_pcm_delay(PCM, &delay) < 0 || delay < 0)
                delay = static_cast<snd_pcm_sframes_t>(buffer_frames_);

            int64_t delay_us = (static_cast<int64_t>(delay) * INT64_C(1000000))
                               / static_cast<int64_t>(sample_rate_);
            int64_t now_us =
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count();

            on_frames_played(static_cast<uint32_t>(offset), now_us + delay_us);
        }
    }
}

#undef PCM
