#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace platform {

struct CaptureFrame {
    std::vector<uint8_t> png;
    int sourceWidth = 0;
    int sourceHeight = 0;
};

class IScreenCapture {
public:
    virtual ~IScreenCapture() = default;

    virtual CaptureFrame capture() = 0;
    virtual void takeScreenshot(const std::string& dir, const std::string& name) = 0;
};

} // namespace platform
