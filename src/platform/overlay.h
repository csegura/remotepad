#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace platform {

struct StrokePoint {
    double x = 0;
    double y = 0;
    double lineWidth = 1.0;
    uint32_t color = 0;
};

enum class OverlayAction {
    Screenshot,
    Activate,
    Deactivate,
    ColorRed,
    ColorGreen,
    ColorBlue,
    Clear,
    Undo,
    Terminate,
};

class IOverlay {
public:
    virtual ~IOverlay() = default;

    virtual bool init() = 0;
    virtual int getEventFd() const = 0;
    virtual std::vector<OverlayAction> pollActions() = 0;

    virtual void activate() = 0;
    virtual void deactivate() = 0;
    virtual bool isActive() const = 0;

    virtual void drawStroke(const std::vector<StrokePoint>& points) = 0;
    virtual void clear() = 0;
    virtual void redraw(const std::vector<std::vector<StrokePoint>>& history) = 0;

    virtual std::string targetName() const = 0;
};

} // namespace platform
