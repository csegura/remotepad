#pragma once

#include "remote_pad.h"
#include <atomic>
#include <functional>
#include <string>
#include <thread>

// Forward declarations - avoid including uWS headers here
struct us_listen_socket_t;
namespace uWS {
class Loop;
}

class WebServer {
public:
    WebServer(int port, const std::string& staticDir, RemotePad& pad);

    // Start the server (runs the uWS event loop)
    void run();

    // Graceful shutdown: clear overlay draws, then stop the server
    void shutdown();

    // Stop the server (close all sockets)
    void stop();

private:
    void watchPlatform();
    void processPlatformEvents();
    static std::string getMimeType(const std::string& path);
    static std::string readFile(const std::string& path);

    int port_;
    std::string staticDir_;
    RemotePad& pad_;
    struct us_listen_socket_t* listenSocket_ = nullptr;
    uWS::Loop* loop_ = nullptr;
    std::atomic<bool> running_{false};
    std::thread platformWatcher_;
    std::function<void()> closeApp_;
};
