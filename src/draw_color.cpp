#include "draw_color.h"

#include <cstdlib>

namespace draw {

uint32_t parseWebColor(const std::string& color) {
    if (color.empty()) {
        return 0;
    }
    if (color[0] == '#') {
        return static_cast<uint32_t>(std::strtoul(color.c_str() + 1, nullptr, 16));
    }
    return static_cast<uint32_t>(std::strtoul(color.c_str(), nullptr, 0));
}

} // namespace draw
