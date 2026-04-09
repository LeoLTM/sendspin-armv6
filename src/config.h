#pragma once

#include <string>

/// Configuration loaded from /etc/sendspin-armv6.conf
struct Config {
    std::string server_url;   // e.g. ws://192.168.1.10:8928/sendspin
    std::string name = "sendspin-armv6";
    std::string log_level = "info";
    std::string device;       // ALSA device, e.g. plughw:1,0 (empty = system default)
    int initial_volume = -1;  // 0-100 to override hardware volume on startup; -1 = server default
};

/// Parse a simple key=value config file.
/// Returns false and prints an error if the file cannot be read.
bool load_config(const std::string& path, Config& config);
