#include "alsa_pipe_sink.h"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>

AlsaPipeSink::~AlsaPipeSink() { stop(); }

void AlsaPipeSink::set_device(const std::string& device) {
    // Validate: allow only characters that are safe in a shell argument.
    // ALSA device names contain alphanumerics, colon, comma, underscore,
    // hyphen, period and equals (e.g. "plughw:1,0", "sysdefault:CARD=Device").
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

bool AlsaPipeSink::configure(uint32_t sample_rate, uint8_t channels,
                             uint8_t bits_per_sample) {
    stop();

    sample_rate_ = sample_rate;
    channels_ = channels;
    bits_per_sample_ = bits_per_sample;
    bytes_per_frame_ = channels * (bits_per_sample / 8);
    buffered_bytes_ = 0;

    const char* format;
    switch (bits_per_sample) {
        case 16: format = "S16_LE"; break;
        case 24: format = "S24_3LE"; break;
        case 32: format = "S32_LE"; break;
        default:
            fprintf(stderr, "AlsaPipeSink: unsupported bit depth %u\n",
                    bits_per_sample);
            return false;
    }

    // Build aplay command. -q suppresses verbose output, -t raw for raw PCM.
    //
    // --buffer-time pins the ALSA hardware buffer to ALSA_BUFFER_TIME_US so
    // its depth is predictable and matches the fixed_delay_us declared in the
    // PlayerRole config.  Without this the kernel default (~500 ms) varies by
    // driver and causes the sync task to under-predict actual playback time.
    //
    // --period-time=20000 (20 ms) is a comfortable interrupt interval for the
    // Pi Zero's single-core scheduler: short enough for timely refill, long
    // enough to avoid excessive CPU wake-ups.
    char cmd[256];
    if (!device_.empty()) {
        snprintf(cmd, sizeof(cmd),
                 "aplay -D %s -f %s -r %u -c %u -t raw -q"
                 " --buffer-time=%d --period-time=20000",
                 device_.c_str(), format, sample_rate, channels,
                 ALSA_BUFFER_TIME_US);
    } else {
        snprintf(cmd, sizeof(cmd),
                 "aplay -f %s -r %u -c %u -t raw -q"
                 " --buffer-time=%d --period-time=20000",
                 format, sample_rate, channels, ALSA_BUFFER_TIME_US);
    }

    pipe_ = popen(cmd, "w");
    if (!pipe_) {
        fprintf(stderr, "AlsaPipeSink: failed to open aplay pipe\n");
        return false;
    }

    fprintf(stderr, "AlsaPipeSink: playing %uHz %uch %ubit via aplay\n",
            sample_rate, channels, bits_per_sample);
    return true;
}

size_t AlsaPipeSink::write(uint8_t* data, size_t length,
                           uint32_t /*timeout_ms*/) {
    if (!pipe_) return length;  // discard if no pipe

    apply_volume(data, length);

    size_t written = fwrite(data, 1, length, pipe_);

    // Report timing feedback for synchronization.
    //
    // The sync task uses the timestamp passed to notify_audio_played() as
    // "the time the last reported frame finished coming out of the DAC".
    // aplay buffers audio in the kernel before it reaches the hardware, so
    // we must report a future timestamp: now + the time the bytes currently
    // sitting in aplay's buffer still need to play.
    if (written > 0 && bytes_per_frame_ > 0 && sample_rate_ > 0 && on_frames_played) {
        buffered_bytes_ += written;
        uint32_t frames = static_cast<uint32_t>(written / bytes_per_frame_);

        // finish_timestamp is the wall-clock time when the last frame now in
        // aplay's buffer will finish playing.  The sync task interprets it as
        // "these frames finished at finish_timestamp" and tracks additionally
        // buffered frames via its own buffered_frames counter.  We therefore
        // report the full aplay buffer depth (including this write) so that
        //
        //   new_playtime = finish_timestamp + sync_task_unplayed_us
        //
        // converges to the correct value once the sync task drains its own
        // outstanding frame count down to match ours.
        int64_t buffered_frames_total = static_cast<int64_t>(buffered_bytes_ / bytes_per_frame_);
        int64_t buffered_us = (buffered_frames_total * INT64_C(1000000)) / static_cast<int64_t>(sample_rate_);

        int64_t now_us = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::steady_clock::now().time_since_epoch())
                             .count();
        int64_t finish_timestamp = now_us + buffered_us;

        // Advance our own estimate: deduct the frames we just reported so
        // that the next write's buffered_us reflects only what remains.
        size_t reported_bytes = static_cast<size_t>(frames) * bytes_per_frame_;
        buffered_bytes_ = (reported_bytes <= buffered_bytes_) ? (buffered_bytes_ - reported_bytes) : 0;

        on_frames_played(frames, finish_timestamp);
    }

    return written;
}

void AlsaPipeSink::stop() {
    if (pipe_) {
        pclose(pipe_);
        pipe_ = nullptr;
    }
    buffered_bytes_ = 0;
}

void AlsaPipeSink::clear() { stop(); }

void AlsaPipeSink::set_volume(uint8_t volume) {
    volume_ = volume > 100 ? 100 : volume;
}

void AlsaPipeSink::set_muted(bool muted) { muted_ = muted; }

void AlsaPipeSink::apply_volume(uint8_t* data, size_t length) {
    if (!muted_ && volume_ >= 100) return;

    if (muted_ || volume_ == 0) {
        std::memset(data, 0, length);
        return;
    }

    // Quadratic curve for perceptually uniform volume control.
    // scale = (vol/100)^2 in Q32 fixed-point.
    static constexpr uint64_t Q32_ONE = UINT64_C(1) << 32;
    static constexpr int FRAC_BITS = 32;
    static constexpr int64_t ROUND = INT64_C(1) << (FRAC_BITS - 1);

    uint64_t v = volume_;
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
