/// @file Headless Sendspin client daemon for ARMv6 (Raspberry Pi Zero W).
///
/// Connects to a Sendspin server and plays audio through the 3.5mm jack
/// via aplay (ALSA).  Designed to run as a systemd service.

#include "sendspin/client.h"
#include "sendspin/player_role.h"
#include "sendspin/metadata_role.h"

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
static constexpr const char* VERSION = "0.1.0";

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
    fprintf(stderr, "  -h        Show this help\n");
    fprintf(stderr, "  -V        Show version\n");
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Parse command line
    std::string config_path = DEFAULT_CONFIG_PATH;
    int opt;
    while ((opt = getopt(argc, argv, "c:hV")) != -1) {
        switch (opt) {
            case 'c': config_path = optarg; break;
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
    PlayerRole::Config player_config;
    player_config.audio_formats = {
        {SendspinCodecFormat::FLAC, 2, 44100, 16},
        {SendspinCodecFormat::FLAC, 2, 48000, 16},
        {SendspinCodecFormat::OPUS, 2, 48000, 16},
        {SendspinCodecFormat::PCM, 2, 44100, 16},
        {SendspinCodecFormat::PCM, 2, 48000, 16},
    };
    auto& player = client.add_player(std::move(player_config));


    // Add metadata role (for logging track info)
    auto& metadata = client.add_metadata();

    // Audio output via aplay pipe
    AlsaPipeSink audio_sink;
    if (!cfg.device.empty()) {
        audio_sink.set_device(cfg.device);
    }

    // --- Listener implementations ---

    struct ArmPlayerListener : PlayerRoleListener {
        AlsaPipeSink& sink;
        PlayerRole& player;
        ArmPlayerListener(AlsaPipeSink& s, PlayerRole& p) : sink(s), player(p) {}

        size_t on_audio_write(uint8_t* data, size_t length,
                              uint32_t timeout_ms) override {
            return sink.write(data, length, timeout_ms);
        }

        void on_stream_start() override {
            fprintf(stderr, ">>> Stream started\n");
            auto& params = player.get_current_stream_params();
            if (params.sample_rate.has_value() && params.channels.has_value() &&
                params.bit_depth.has_value()) {
                sink.configure(*params.sample_rate, *params.channels,
                               *params.bit_depth);
            }
        }

        void on_stream_end() override {
            fprintf(stderr, ">>> Stream ended\n");
            sink.clear();
        }

        void on_stream_clear() override {
            fprintf(stderr, ">>> Stream clear\n");
            sink.clear();
        }

        void on_volume_changed(uint8_t vol) override { sink.set_volume(vol); }
        void on_mute_changed(bool muted) override { sink.set_muted(muted); }
    };

    struct ArmMetadataListener : MetadataRoleListener {
        void on_metadata(const ServerMetadataStateObject& md) override {
            if (md.title.has_value()) {
                fprintf(stderr, ">>> Now playing: %s - %s\n",
                        md.artist.value_or("Unknown").c_str(),
                        md.title->c_str());
            }
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

    ArmPlayerListener player_listener(audio_sink, player);
    audio_sink.on_frames_played = [&player](uint32_t frames, int64_t timestamp) {
        player.notify_audio_played(frames, timestamp);
    };

    ArmMetadataListener metadata_listener;
    ArmClientListener client_listener;
    HostNetworkProvider network_provider;

    player.set_listener(&player_listener);
    metadata.set_listener(&metadata_listener);
    client.set_listener(&client_listener);
    client.set_network_provider(&network_provider);

    // Start the WebSocket server
    fprintf(stderr, "sendspin-armv6 %s — %s\n", VERSION, cfg.name.c_str());

    if (!client.start_server()) {
        fprintf(stderr, "Failed to start server\n");
        return 1;
    }

    // Connect to the configured Sendspin server
    fprintf(stderr, "Connecting to %s...\n", cfg.server_url.c_str());
    client.connect_to(cfg.server_url);

    // Main loop
    while (running.load()) {
        client.loop();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    fprintf(stderr, "\nShutting down...\n");
    client.disconnect(SendspinGoodbyeReason::SHUTDOWN);
    return 0;
}
