#include "remote_pad.h"

#include "draw_color.h"

#include <filesystem>
#include <iostream>
#include <utility>

RemotePad::RemotePad(std::unique_ptr<platform::IOverlay> overlay,
                     std::unique_ptr<platform::IScreenCapture> screenCapture,
                     const AppConfig& config)
    : overlay_(std::move(overlay)),
      screenCapture_(std::move(screenCapture)),
      currentColor_(draw::kGreen),
      config_(config) {}

bool RemotePad::init() {
    if (!overlay_ || !screenCapture_) {
        std::cerr << "RemotePad requires overlay and screen capture backends" << std::endl;
        return false;
    }

    if (!overlay_->init()) {
        return false;
    }

    targetName_ = overlay_->targetName();
    return true;
}

void RemotePad::processEvents() {
    for (const auto action : overlay_->pollActions()) {
        applyOverlayAction(action);
        if (terminate_) {
            return;
        }
    }
}

bool RemotePad::consumeClearFlag() {
    bool was = clearPending_;
    clearPending_ = false;
    return was;
}

void RemotePad::drawStart(double x, double y, double lineWidth, const std::string& color) {
    const uint32_t strokeColor = color.empty() ? currentColor_ : draw::parseWebColor(color);
    currentColor_ = strokeColor;
    points_.clear();
    points_.push_back({
        .x = x,
        .y = y,
        .lineWidth = lineWidth * lineWidthFactor_,
        .color = strokeColor,
    });
    drawing_ = true;
}

void RemotePad::drawMove(double x, double y, double lineWidth, const std::string& color) {
    if (!drawing_) {
        return;
    }

    const uint32_t strokeColor = color.empty() ? currentColor_ : draw::parseWebColor(color);
    currentColor_ = strokeColor;
    points_.push_back({
        .x = x,
        .y = y,
        .lineWidth = lineWidth * lineWidthFactor_,
        .color = strokeColor,
    });
    overlay_->drawStroke(points_);
}

void RemotePad::drawEnd() {
    if (!points_.empty()) {
        drawHistory_.push_back(points_);
    }
    points_.clear();
    drawing_ = false;
}

void RemotePad::drawClear() {
    clearDraw(false);
}

void RemotePad::undo() {
    clearDraw(true);
}

void RemotePad::activateOverlay() {
    overlay_->activate();
}

void RemotePad::deactivateOverlay() {
    overlay_->deactivate();
}

platform::CaptureFrame RemotePad::captureScreen() {
    const bool wasVisible = overlay_->isActive();
    if (wasVisible) {
        overlay_->deactivate();
    }

    auto frame = screenCapture_->capture();

    if (wasVisible) {
        overlay_->activate();
        overlay_->redraw(drawHistory_);
    }

    return frame;
}

void RemotePad::takeScreenshot(const std::string& dir) {
    std::filesystem::create_directories(dir);
    screenCapture_->takeScreenshot(dir, targetName_);
}

int RemotePad::getEventFd() const {
    return overlay_->getEventFd();
}

void RemotePad::clearDraw(bool redrawHistory) {
    overlay_->clear();

    if (!redrawHistory) {
        drawHistory_.clear();
        points_.clear();
        return;
    }

    if (!drawHistory_.empty()) {
        drawHistory_.pop_back();
    }
    points_.clear();
    overlay_->redraw(drawHistory_);
}

void RemotePad::applyOverlayAction(platform::OverlayAction action) {
    switch (action) {
    case platform::OverlayAction::Screenshot:
        std::cout << "Taking screenshot" << std::endl;
        takeScreenshot("./screenshots");
        break;
    case platform::OverlayAction::Activate:
        activateOverlay();
        break;
    case platform::OverlayAction::Deactivate:
        deactivateOverlay();
        break;
    case platform::OverlayAction::ColorRed:
        currentColor_ = draw::kRed;
        break;
    case platform::OverlayAction::ColorGreen:
        currentColor_ = draw::kGreen;
        break;
    case platform::OverlayAction::ColorBlue:
        currentColor_ = draw::kBlue;
        break;
    case platform::OverlayAction::Clear:
        clearDraw(false);
        clearPending_ = true;
        break;
    case platform::OverlayAction::Undo:
        clearDraw(true);
        break;
    case platform::OverlayAction::Terminate:
        terminate_ = true;
        break;
    }
}
