#pragma once

#include <cstdint>
#include <string>

namespace draw {

constexpr uint32_t kGreen = 0xADFF2F;
constexpr uint32_t kBlue = 0x54C2CC;
constexpr uint32_t kRed = 0xDE3700;

uint32_t parseWebColor(const std::string& color);

} // namespace draw
