/// @file Headless Sendspin client daemon for ARMv6 (Raspberry Pi Zero W).
///
/// Connects to a Sendspin server and plays audio through the 3.5mm jack
/// via aplay (ALSA).  Designed to run as a systemd service.

#include "sendspin/client.h"
#include "sendspin/player_role.h"

#include "alsa_pipe_sink.h"
#include "config.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <getopt.h>
#include <string>
#include <thread>
#include <unistd.h>

using namespace sendspin;

static constexpr const char* DEFAULT_CONFIG_PATH = "/etc/sendspin-armv6.conf";
static constexpr const char* VERSION = "0.1.7";

static std::atomic<bool> running{true};

static void signal_handler(int /*sig*/) { running.store(false); }

static bool parse_log_level(const std::string& str, LogLevel& level) {
    if (str == "none") { level = LogLevel::NONE; return true; }
    if (str == "error") { level = LogLevel::ERROR; return true; }
    if (str == "warn") { level = LogLevel::WARN; return true; }
    if (str == "info") { level = LogLevel::INFO; return true; }
    if (str == "debug") { level = LogLevel::DEBUG; return true; }
    if (str == "verbose") { level = LogLevel::VERBOSE; return true; }
    return false;
}

static void print_usage(const char* prog) {
    fprintf(stderr, "Usage: %s [options]\n\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -c PATH   Config file (default: %s)\n", DEFAULT_CONFIG_PATH);
    fprintf(stderr, "  -t SECS   Idle timeout in seconds before releasing audio device (0=disable)\n");
    fprintf(stderr, "  -h        Show this help\n");
    fprintf(stderr, "  -V        Show version\n");
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Parse command line
    std::string config_path = DEFAULT_CONFIG_PATH;
    int idle_timeout_override = -1;
    int opt;
    while ((opt = getopt(argc, argv, "c:t:hV")) != -1) {
        switch (opt) {
            case 'c': config_path = optarg; break;
            case 't': idle_timeout_override = std::atoi(optarg); break;
            case 'h': print_usage(argv[0]); return 0;
            case 'V': fprintf(stdout, "sendspin-armv6 %s\n", VERSION); return 0;
            default: print_usage(argv[0]); return 1;
        }
    }

    // Load configuration
    Config cfg;
    if (!load_config(config_path, cfg)) {
        return 1;
    }

    if (idle_timeout_override >= 0) {
        cfg.idle_timeout_s = idle_timeout_override;
    }

    if (cfg.server_url.empty()) {
        fprintf(stderr, "Error: server_url is required in %s\n", config_path.c_str());
        return 1;
    }

    // Apply log level
    LogLevel log_level = LogLevel::INFO;
    if (!parse_log_level(cfg.log_level, log_level)) {
        fprintf(stderr, "Warning: unknown log level '%s', using 'info'\n",
                cfg.log_level.c_str());
    }
    SendspinClient::set_log_level(log_level);

    // --- STARTUP LOGGING BLOCK ---
    fprintf(stderr, "\n");
    fprintf(stderr, "=================================================================\n");
    fprintf(stderr, " Sendspin ARMv6 Client - Version: %s\n", VERSION);
    fprintf(stderr, " Build Date: %s %s\n", __DATE__, __TIME__);
    fprintf(stderr, "=================================================================\n");
    fprintf(stderr, " Active Configuration:\n");
    fprintf(stderr, "  Config File   : %s\n", config_path.c_str());
    fprintf(stderr, "  Friendly Name : %s\n", cfg.name.c_str());
    fprintf(stderr, "  Audio Device  : %s\n", cfg.device.empty() ? "System Default" : cfg.device.c_str());
    fprintf(stderr, "  Server URL    : %s\n", cfg.server_url.c_str());
    fprintf(stderr, "  Idle Timeout  : %s\n", cfg.idle_timeout_s == 0 ? "inaktiv" : (std::to_string(cfg.idle_timeout_s) + " seconds").c_str());
    
    const char* log_level_str = "info";
    switch (log_level) {
        case LogLevel::NONE: log_level_str = "none"; break;
        case LogLevel::ERROR: log_level_str = "error"; break;
        case LogLevel::WARN: log_level_str = "warn"; break;
        case LogLevel::INFO: log_level_str = "info"; break;
        case LogLevel::DEBUG: log_level_str = "debug"; break;
        case LogLevel::VERBOSE: log_level_str = "verbose"; break;
    }
    fprintf(stderr, "  Log Level     : %s\n", log_level_str);
    fprintf(stderr, "=================================================================\n\n");
    // --- END STARTUP LOGGING BLOCK ---

    // Derive a unique, persistent client_id from /etc/machine-id (systemd).
    // Falls back to hostname.  The spec requires this to be unique per device
    // so that servers can track group membership and de-duplicate connections.
    std::string client_id;
    {
        std::ifstream mid("/etc/machine-id");
        std::getline(mid, client_id);
    }
    if (client_id.empty()) {
        char hbuf[256] = {};
        if (gethostname(hbuf, sizeof(hbuf)) == 0 && hbuf[0] != '\0') {
            client_id = hbuf;
        } else {
            client_id = "sendspin-armv6";
        }
    }

    // Configure client
    SendspinClientConfig client_config;
    client_config.client_id = "sendspin-armv6-" + client_id;
    client_config.name = cfg.name;
    client_config.product_name = "sendspin-armv6";
    client_config.manufacturer = "sendspin-armv6";
    client_config.software_version = VERSION;

    SendspinClient client(std::move(client_config));

    // Add player role with supported audio formats
    PlayerRoleConfig player_config;
    player_config.audio_formats = {
        {SendspinCodecFormat::FLAC, 2, 44100, 16},
        {SendspinCodecFormat::FLAC, 2, 48000, 16},
        {SendspinCodecFormat::OPUS, 2, 48000, 16},
        {SendspinCodecFormat::PCM, 2, 44100, 16},
        {SendspinCodecFormat::PCM, 2, 48000, 16},
    };
    // Larger ring buffer absorbs CPU scheduling jitter on the single-core ARMv6
    // without dropping encoded chunks before the sync task can decode them.
    player_config.audio_buffer_capacity = 2000000;
    // With direct ALSA and snd_pcm_delay()-based timestamps, the reported
    // playback time already accounts for the hardware buffer.  No additional
    // fixed delay compensation is required.
    player_config.fixed_delay_us = AlsaPipeSink::PIPELINE_DELAY_US;
    if (cfg.initial_static_delay_ms >= 0) {
        player_config.initial_static_delay_ms =
            static_cast<uint16_t>(cfg.initial_static_delay_ms);
    }
    auto& player = client.add_player(std::move(player_config));

    // Audio output via aplay pipe
    AlsaPipeSink audio_sink;
    if (!cfg.device.empty()) {
        audio_sink.set_device(cfg.device);
    }

    // --- Listener implementations ---

    struct ArmPlayerListener : PlayerRoleListener {
        bool stream_active{false};
        bool device_open{false};
        std::chrono::steady_clock::time_point last_stream_end_time{std::chrono::steady_clock::now()};
        int idle_timeout_s{0};

        AlsaPipeSink& sink;
        PlayerRole& player;
        ArmPlayerListener(AlsaPipeSink& s, PlayerRole& p, int timeout_s) 
            : sink(s), player(p), idle_timeout_s(timeout_s) {}

        size_t on_audio_write(uint8_t* data, size_t length,
                              uint32_t timeout_ms) override {
            return sink.write(data, length, timeout_ms);
        }

        void on_stream_start() override {
            fprintf(stderr, ">>> Stream started\n");
            this->stream_active = true;
            this->device_open = true;
            auto& params = player.get_current_stream_params();
            if (params.sample_rate.has_value() && params.channels.has_value() &&
                params.bit_depth.has_value()) {
                sink.configure(*params.sample_rate, *params.channels,
                               *params.bit_depth);
            }
        }

        void on_stream_end() override {
            fprintf(stderr, ">>> Stream ended\n");
            this->stream_active = false;
            this->last_stream_end_time = std::chrono::steady_clock::now();
            
            if (this->idle_timeout_s > 0) {
                fprintf(stderr, ">>> Audio device will be released in %d seconds if idle\n", this->idle_timeout_s);
            }
            sink.clear();
        }

        void on_volume_changed(uint8_t vol) override { sink.set_volume(vol); }
        void on_mute_changed(bool muted) override { sink.set_muted(muted); }
        void on_static_delay_changed(uint16_t delay_ms) override {
            fprintf(stderr, ">>> Static delay changed to %u ms\n", delay_ms);
        }
    };

    struct ArmClientListener : SendspinClientListener {
        void on_time_sync_updated(float error) override {
            if (SendspinClient::get_log_level() >= LogLevel::DEBUG) {
                fprintf(stderr, ">>> Time sync error: %.1f us\n", error);
            }
        }
    };

    struct HostNetworkProvider : SendspinNetworkProvider {
        bool is_network_ready() override { return true; }
    };

    ArmPlayerListener player_listener(audio_sink, player, cfg.idle_timeout_s);
    audio_sink.on_frames_played = [&player](uint32_t frames, int64_t timestamp) {
        player.notify_audio_played(frames, timestamp);
    };

    ArmClientListener client_listener;
    HostNetworkProvider network_provider;

    player.set_listener(&player_listener);
    player.set_static_delay_adjustable(true);
    client.set_listener(&client_listener);
    client.set_network_provider(&network_provider);

    // Start the WebSocket server
    if (!client.start_server()) {
        fprintf(stderr, "Failed to start server\n");
        return 1;
    }

    // Apply the configured startup volume before the first connection so the
    // hardware is at the expected level even before the server issues a
    // VOLUME command.  Without this the ALSA sink defaults to 100 while the
    // SDK reports 0 until the server speaks.
    if (cfg.initial_volume >= 0) {
        auto vol = static_cast<uint8_t>(cfg.initial_volume);
        audio_sink.set_volume(vol);
        player.update_volume(vol);
        fprintf(stderr, "Initial volume set to %u\n", vol);
    }

    // Connect to the configured Sendspin server and reconnect automatically
    // whenever the connection drops (e.g. server restart mid-playback).
    // The first attempt fires immediately; subsequent ones every 5 seconds.
    using clock = std::chrono::steady_clock;
    static constexpr auto RECONNECT_INTERVAL = std::chrono::seconds(5);
    auto last_connect_attempt = clock::now() - RECONNECT_INTERVAL;

    while (running.load()) {
        client.loop();

        // Idle timeout check to release audio device
        if (cfg.idle_timeout_s > 0 && !player_listener.stream_active && player_listener.device_open) {
            auto now = clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - player_listener.last_stream_end_time).count();
            if (elapsed >= cfg.idle_timeout_s) {
                fprintf(stderr, ">>> Idle timeout reached (%d s), releasing audio device\n", cfg.idle_timeout_s);
                audio_sink.stop();
                player_listener.device_open = false;
            }
        }

        if (!client.is_connected()) {
            auto now = clock::now();
            if (now - last_connect_attempt >= RECONNECT_INTERVAL) {
                fprintf(stderr, "Connecting to %s...\n", cfg.server_url.c_str());
                client.connect_to(cfg.server_url);
                last_connect_attempt = now;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    fprintf(stderr, "\nShutting down...\n");
    client.disconnect(SendspinGoodbyeReason::SHUTDOWN);
    return 0;
}