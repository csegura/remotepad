#include "platform/linux/linux_screen_capture.h"

#include "x_screen.h"

LinuxScreenCapture::LinuxScreenCapture(Display* dpy, Window root, Window target)
    : dpy_(dpy), root_(root), target_(target) {}

platform::CaptureFrame LinuxScreenCapture::capture() {
    const auto frame = xscreen::getBufferedScreen(dpy_, root_, target_);
    return {
        .png = frame.png,
        .sourceWidth = frame.sourceWidth,
        .sourceHeight = frame.sourceHeight,
    };
}

void LinuxScreenCapture::takeScreenshot(const std::string& dir, const std::string& name) {
    xscreen::takeScreenshot(dpy_, root_, target_, dir, name);
}
