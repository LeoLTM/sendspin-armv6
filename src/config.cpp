#include "config.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>

static std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
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
        } else {
            fprintf(stderr, "%s:%d: unknown key '%s'\n", path.c_str(), line_num,
                    key.c_str());
        }
    }

    return true;
}
