#pragma once

#include <string>

/// Configuration loaded from /etc/sendspin-armv6.conf
struct Config {
    std::string server_url;            // e.g. ws://192.168.1.10:8927/sendspin
    std::string name = "sendspin-armv6";
    std::string log_level = "info";
    std::string device;                // ALSA device, e.g. plughw:1,0 (empty = system default)
    int initial_volume = -1;           // 0-100 to override hardware volume on startup; -1 = server default
    int initial_static_delay_ms = -1;  // 0-5000 ms; -1 = no initial delay
    int idle_timeout_s = 0;            // 0 = disabled (device stays open permanently)

    // Optional now-playing OLED display (disabled unless display is set)
    std::string display = "none";      // none, ssd1306, sh1106
    int display_i2c_bus = 1;           // /dev/i2c-<bus>
    int display_i2c_address = 0x3C;
    int display_height = 64;           // 64 or 32
    int display_rotate = 0;            // 0 or 180
    int display_contrast = 128;        // 0-255
    int display_sleep_s = 300;         // blank after this long without playback; 0 = never
};

/// Parse a simple key=value config file.
/// Returns false and prints an error if the file cannot be read.
bool load_config(const std::string& path, Config& config);
