#include "web_server.h"
#include "embedded_client.h"

#include <App.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string_view>
#include <thread>

#ifndef _WIN32
#include <poll.h>
#endif

using json = nlohmann::json;
namespace fs = std::filesystem;

struct PerSocketData {
    uint64_t captureSeq = 0;
};
using WsType = uWS::WebSocket<false, true, PerSocketData>;

static void sendControlEvent(WsType* ws, const std::string& event, const std::string& message = "") {
    json payload = {{"event", event}};
    if (!message.empty()) {
        payload["data"] = {{"message", message}};
    }
    ws->send(payload.dump(), uWS::OpCode::TEXT);
}

static bool decodeDrawPayload(const json& data, double& x, double& y, double& lineWidth, std::string& color) {
    if (!data.is_object()) {
        return false;
    }
    if (!data.contains("x") || !data["x"].is_number()) return false;
    if (!data.contains("y") || !data["y"].is_number()) return false;
    if (!data.contains("lineWidth") || !data["lineWidth"].is_number()) return false;
    if (!data.contains("color") || !data["color"].is_string()) return false;

    x = data["x"].get<double>();
    y = data["y"].get<double>();
    lineWidth = data["lineWidth"].get<double>();
    color = data["color"].get<std::string>();
    return true;
}

WebServer::WebServer(int port, const std::string& staticDir, RemotePad& pad)
    : port_(port), staticDir_(staticDir), pad_(pad) {}

std::string WebServer::getMimeType(const std::string& path) {
    if (path.ends_with(".html")) return "text/html";
    if (path.ends_with(".css")) return "text/css";
    if (path.ends_with(".js")) return "application/javascript";
    if (path.ends_with(".json")) return "application/json";
    if (path.ends_with(".png")) return "image/png";
    if (path.ends_with(".jpg") || path.ends_with(".jpeg")) return "image/jpeg";
    if (path.ends_with(".svg")) return "image/svg+xml";
    return "application/octet-stream";
}

std::string WebServer::readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return "";
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

void WebServer::processPlatformEvents() {
    pad_.processEvents();
    if (pad_.shouldTerminate()) {
        stop();
    }
}

void WebServer::watchPlatform() {
    const int eventFd = pad_.getEventFd();

    while (running_) {
#ifndef _WIN32
        int result = 0;
        short revents = 0;
        if (eventFd >= 0) {
            pollfd fd{
                .fd = eventFd,
                .events = POLLIN,
                .revents = 0,
            };
            result = poll(&fd, 1, 200);
            revents = fd.revents;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        constexpr int result = 1;
        constexpr short revents = 0;
#endif

        if (!running_) {
            return;
        }

        const bool shouldProcess = eventFd < 0 || (result > 0 && (revents & POLLIN));
        if (shouldProcess && loop_ != nullptr) {
            loop_->defer([this]() { processPlatformEvents(); });
        }
    }
}

static void handleMessage(WsType* ws, std::string_view message, RemotePad& pad) {
    try {
        auto msg = json::parse(message);
        const std::string event = msg.value("event", "");
        auto data = msg.value("data", json::object());

        if (event == "capture") {
            sendControlEvent(ws, "capture_started");
            auto* perSocket = ws->getUserData();
            perSocket->captureSeq += 1;
            const uint64_t captureSeq = perSocket->captureSeq;
            const auto captureTs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            auto frame = pad.captureScreen();
            if (!frame.png.empty()) {
                json ready = {
                    {"event", "capture_ready"},
                    {"data", {
                        {"capture_seq", captureSeq},
                        {"capture_ts", captureTs},
                        {"sourceWidth", frame.sourceWidth},
                        {"sourceHeight", frame.sourceHeight}
                    }}
                };
                ws->send(ready.dump(), uWS::OpCode::TEXT);
                ws->send(
                    std::string_view(
                        reinterpret_cast<const char*>(frame.png.data()),
                        frame.png.size()),
                    uWS::OpCode::BINARY);
            } else {
                sendControlEvent(ws, "capture_failed", "Screen capture failed");
            }
        } else if (event == "drawstart") {
            double x = 0.0;
            double y = 0.0;
            double lineWidth = 3.0;
            std::string color;
            if (!decodeDrawPayload(data, x, y, lineWidth, color)) {
                sendControlEvent(ws, "error", "Invalid drawstart payload");
                return;
            }
            pad.activateOverlay();
            pad.drawStart(x, y, lineWidth, color);
        } else if (event == "drawmove") {
            double x = 0.0;
            double y = 0.0;
            double lineWidth = 3.0;
            std::string color;
            if (!decodeDrawPayload(data, x, y, lineWidth, color)) {
                sendControlEvent(ws, "error", "Invalid drawmove payload");
                return;
            }
            pad.drawMove(x, y, lineWidth, color);
        } else if (event == "drawend") {
            pad.drawEnd();
        } else if (event == "drawclear") {
            pad.drawClear();
        } else if (event == "undo") {
            pad.undo();
        } else if (event == "screenshot") {
            pad.takeScreenshot("./screenshots");
            sendControlEvent(ws, "screenshot_saved", "Screenshot saved");
        } else if (event == "end") {
            pad.drawClear();
            pad.deactivateOverlay();
        }
    } catch (const std::exception& e) {
        std::cerr << "WebSocket message error: " << e.what() << std::endl;
        sendControlEvent(ws, "error", "WebSocket message parse error");
    }
}

void WebServer::run() {
    uWS::App app;

    RemotePad* padPtr = &pad_;
    const std::string staticDir = staticDir_;
    running_ = true;

    app.ws<PerSocketData>("/*", uWS::App::WebSocketBehavior<PerSocketData>{
        .open = [](WsType* ws) {
            auto* perSocket = ws->getUserData();
            perSocket->captureSeq = 0;
            std::cout << "Client connected" << std::endl;
        },
        .message = [padPtr](WsType* ws, std::string_view message, uWS::OpCode /*opCode*/) {
            handleMessage(ws, message, *padPtr);
        },
        .close = [](WsType* /*ws*/, int /*code*/, std::string_view /*message*/) {
            std::cout << "Client disconnected" << std::endl;
        }
    }).get("/*", [staticDir](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        std::string url(req->getUrl());
        if (url == "/") url = "/index.html";

#ifdef EMBED_CLIENT
        const auto* embedded = findEmbeddedFile(url);
        if (embedded) {
            res->writeHeader("Content-Type", embedded->mime)
               ->writeHeader("Cache-Control", "no-cache")
               ->end(std::string_view(
                   reinterpret_cast<const char*>(embedded->data),
                   embedded->size));
            return;
        }
#endif

        const std::string filePath = staticDir + url;
        const auto canonical = fs::weakly_canonical(filePath);
        const auto baseCanonical = fs::weakly_canonical(staticDir);
        if (canonical.string().find(baseCanonical.string()) != 0) {
            res->writeStatus("403 Forbidden")->end("Forbidden");
            return;
        }

        if (fs::exists(filePath) && fs::is_regular_file(filePath)) {
            const std::string content = readFile(filePath);
            const std::string mime = getMimeType(filePath);
            res->writeHeader("Content-Type", mime)
               ->writeHeader("Cache-Control", "no-cache")
               ->end(content);
        } else {
            res->writeStatus("404 Not Found")->end("Not Found");
        }
    }).listen(port_, [this](auto* socket) {
        listenSocket_ = socket;
        if (socket) {
            std::cout << "RemotePad v" << REMOTEPAD_VERSION
                      << " - Running on port " << port_ << std::endl;
            std::cout << REMOTEPAD_URL << std::endl;
            std::cout << "Quit: CTRL+SHIFT+Q" << std::endl;
        } else {
            std::cerr << "Failed to listen on port " << port_ << std::endl;
        }
    });

    loop_ = uWS::Loop::get();
    platformWatcher_ = std::thread([this]() { watchPlatform(); });

    app.run();

    running_ = false;
    if (platformWatcher_.joinable()) {
        platformWatcher_.join();
    }
}

void WebServer::stop() {
    running_ = false;
    if (listenSocket_) {
        us_listen_socket_close(0, listenSocket_);
        listenSocket_ = nullptr;
    }
}
