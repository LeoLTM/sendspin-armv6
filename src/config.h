#pragma once

#include <string>

/// Configuration loaded from /etc/sendspin-armv6.conf
struct Config {
    std::string server_url;   // e.g. ws://192.168.1.10:8928/sendspin
    std::string name = "sendspin-armv6";
    std::string log_level = "info";
};

/// Parse a simple key=value config file.
/// Returns false and prints an error if the file cannot be read.
bool load_config(const std::string& path, Config& config);
