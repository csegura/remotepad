#pragma once

#include "platform/screen_capture.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>

class WinScreenCapture : public platform::IScreenCapture {
public:
    explicit WinScreenCapture(HWND target);

    platform::CaptureFrame capture() override;
    void takeScreenshot(const std::string& dir, const std::string& name) override;

private:
    HWND target_ = nullptr;
};
