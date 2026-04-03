#pragma once

#include <X11/Xlib.h>
#include <cstdint>
#include <string>
#include <vector>

namespace xscreen {

struct CaptureFrame {
    std::vector<uint8_t> png;
    int sourceWidth = 0;
    int sourceHeight = 0;
};

// Capture a window as a lossless PNG buffer.
CaptureFrame getBufferedScreen(Display* dpy, Window root, Window wid);

// Save a screenshot to file.
void takeScreenshot(Display* dpy, Window root, Window wid,
                    const std::string& dir, const std::string& name);

} // namespace xscreen
