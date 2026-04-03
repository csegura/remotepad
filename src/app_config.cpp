#include "app_config.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>

namespace {

std::string trim(std::string value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }

    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

std::optional<int> parseInt(const std::string& raw) {
    try {
        size_t parsed = 0;
        const int value = std::stoi(raw, &parsed);
        if (parsed != raw.size()) {
            return std::nullopt;
        }
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

void applyConfigValue(AppConfig& config, const std::string& key, const std::string& value) {
    const auto parsed = parseInt(value);
    if (!parsed) {
        return;
    }

    if (key == "SERVER_PORT") {
        config.port = std::max(1, *parsed);
    }
}

void loadDotEnv(AppConfig& config) {
    std::ifstream env(".env");
    if (!env) {
        return;
    }

    std::string line;
    while (std::getline(env, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }

        auto pos = line.find('=');
        if (pos == std::string::npos) {
            pos = line.find(':');
        }
        if (pos == std::string::npos) {
            continue;
        }

        const std::string key = trim(line.substr(0, pos));
        const std::string value = trim(line.substr(pos + 1));
        applyConfigValue(config, key, value);
    }
}

void loadEnvironment(AppConfig& config) {
    const struct {
        const char* name;
    } vars[] = {
        {"SERVER_PORT"},
    };

    for (const auto& var : vars) {
        if (const char* raw = std::getenv(var.name)) {
            applyConfigValue(config, var.name, trim(raw));
        }
    }
}

} // namespace

AppConfig loadConfig() {
    AppConfig config;
    loadDotEnv(config);
    loadEnvironment(config);
    return config;
}
