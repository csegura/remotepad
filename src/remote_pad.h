#pragma once

#include "app_config.h"
#include "platform/overlay.h"
#include "platform/screen_capture.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class RemotePad {
public:
    RemotePad(std::unique_ptr<platform::IOverlay> overlay,
              std::unique_ptr<platform::IScreenCapture> screenCapture,
              const AppConfig& config);

    bool init();
    void processEvents();

    void drawStart(double x, double y, double lineWidth, const std::string& color);
    void drawMove(double x, double y, double lineWidth, const std::string& color);
    void drawEnd();
    void drawClear();
    void undo();
    void activateOverlay();
    void deactivateOverlay();

    platform::CaptureFrame captureScreen();
    void takeScreenshot(const std::string& dir);

    bool shouldTerminate() const { return terminate_; }
    bool consumeClearFlag();
    int getEventFd() const;
    const std::string& targetName() const { return targetName_; }

private:
    void clearDraw(bool redrawHistory);
    void applyOverlayAction(platform::OverlayAction action);

    std::unique_ptr<platform::IOverlay> overlay_;
    std::unique_ptr<platform::IScreenCapture> screenCapture_;

    bool drawing_ = false;
    bool terminate_ = false;
    bool clearPending_ = false;

    std::string targetName_;
    uint32_t currentColor_ = 0;
    AppConfig config_;

    std::vector<platform::StrokePoint> points_;
    std::vector<std::vector<platform::StrokePoint>> drawHistory_;

    static constexpr double lineWidthFactor_ = 1.5;
};
