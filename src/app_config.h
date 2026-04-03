#pragma once

constexpr const char* REMOTEPAD_VERSION = "1.0.0";
constexpr const char* REMOTEPAD_URL = "https://github.com/csegura/remotepad";

struct AppConfig {
    int port = 50005;
};

AppConfig loadConfig();
