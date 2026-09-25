#include "config.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>

static std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

/// Parse an integer key into out if it is within [min, max].  With base 16 an
/// optional 0x prefix is accepted.  Prints a warning and leaves out untouched on error.
static void parse_int_key(const std::string& path, int line_num, const std::string& key,
                          const std::string& value, int min, int max, int& out, int base = 10) {
    try {
        size_t used = 0;
        int v = std::stoi(value, &used, base);
        if (used != value.size()) throw std::invalid_argument(value);
        if (v < min || v > max) {
            fprintf(stderr, "%s:%d: %s must be %d-%d, ignoring\n", path.c_str(), line_num,
                    key.c_str(), min, max);
            return;
        }
        out = v;
    } catch (const std::exception&) {
        fprintf(stderr, "%s:%d: invalid %s value '%s', ignoring\n", path.c_str(), line_num,
                key.c_str(), value.c_str());
    }
}

bool load_config(const std::string& path, Config& config) {
    std::ifstream file(path);
    if (!file.is_open()) {
        fprintf(stderr, "Cannot open config file: %s\n", path.c_str());
        return false;
    }

    std::string line;
    int line_num = 0;
    while (std::getline(file, line)) {
        ++line_num;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        auto eq = line.find('=');
        if (eq == std::string::npos) {
            fprintf(stderr, "%s:%d: expected key=value\n", path.c_str(), line_num);
            continue;
        }

        std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));

        if (key == "server_url") {
            config.server_url = value;
        } else if (key == "name") {
            config.name = value;
        } else if (key == "log_level") {
            config.log_level = value;
        } else if (key == "device") {
            config.device = value;
        } else if (key == "initial_volume") {
            try {
                int v = std::stoi(value);
                if (v < 0 || v > 100) {
                    fprintf(stderr, "%s:%d: initial_volume must be 0-100, ignoring\n",
                            path.c_str(), line_num);
                } else {
                    config.initial_volume = v;
                }
            } catch (const std::exception&) {
                fprintf(stderr, "%s:%d: invalid initial_volume value '%s', ignoring\n",
                        path.c_str(), line_num, value.c_str());
            }
        } else if (key == "initial_static_delay") {
            try {
                int v = std::stoi(value);
                if (v < 0 || v > 5000) {
                    fprintf(stderr, "%s:%d: initial_static_delay must be 0-5000 (ms), ignoring\n",
                            path.c_str(), line_num);
                } else {
                    config.initial_static_delay_ms = v;
                }
            } catch (const std::exception&) {
                fprintf(stderr, "%s:%d: invalid initial_static_delay value '%s', ignoring\n",
                        path.c_str(), line_num, value.c_str());
            }
        } else if (key == "idle_timeout") {
            try {
                int v = std::stoi(value);
                if (v < 0) {
                    fprintf(stderr, "%s:%d: idle_timeout must be >= 0, ignoring\n",
                            path.c_str(), line_num);
                } else {
                    config.idle_timeout_s = v;
                }
            } catch (const std::exception&) {
                fprintf(stderr, "%s:%d: invalid idle_timeout value '%s', ignoring\n",
                        path.c_str(), line_num, value.c_str());
            }
        } else if (key == "display") {
            if (value == "none" || value == "ssd1306" || value == "sh1106") {
                config.display = value;
            } else {
                fprintf(stderr, "%s:%d: display must be none, ssd1306 or sh1106, ignoring\n",
                        path.c_str(), line_num);
            }
        } else if (key == "display_i2c_bus") {
            parse_int_key(path, line_num, key, value, 0, 255, config.display_i2c_bus);
        } else if (key == "display_i2c_address") {
            // Always hex, as printed by i2cdetect: "3c" and "0x3C" both work.
            parse_int_key(path, line_num, key, value, 0x03, 0x77, config.display_i2c_address, 16);
        } else if (key == "display_height") {
            int v = -1;
            parse_int_key(path, line_num, key, value, 32, 64, v);
            if (v == 32 || v == 64) {
                config.display_height = v;
            } else if (v != -1) {
                fprintf(stderr, "%s:%d: display_height must be 32 or 64, ignoring\n",
                        path.c_str(), line_num);
            }
        } else if (key == "display_rotate") {
            int v = -1;
            parse_int_key(path, line_num, key, value, 0, 180, v);
            if (v == 0 || v == 180) {
                config.display_rotate = v;
            } else if (v != -1) {
                fprintf(stderr, "%s:%d: display_rotate must be 0 or 180, ignoring\n",
                        path.c_str(), line_num);
            }
        } else if (key == "display_contrast") {
            parse_int_key(path, line_num, key, value, 0, 255, config.display_contrast);
        } else if (key == "display_sleep") {
            parse_int_key(path, line_num, key, value, 0, 86400, config.display_sleep_s);
        } else {
            fprintf(stderr, "%s:%d: unknown key '%s'\n", path.c_str(), line_num,
                    key.c_str());
        }
    }

    return true;
}
