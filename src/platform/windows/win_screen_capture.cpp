#include "platform/windows/win_screen_capture.h"

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

namespace {

uint32_t adler32Compute(const uint8_t* data, size_t len) {
    constexpr uint32_t kModAdler = 65521;
    uint32_t a = 1;
    uint32_t b = 0;
    for (size_t i = 0; i < len; ++i) {
        a = (a + data[i]) % kModAdler;
        b = (b + a) % kModAdler;
    }
    return (b << 16) | a;
}

uint32_t crc32Compute(const uint8_t* data, size_t len) {
    static uint32_t table[256]{};
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            table[i] = c;
        }
        init = true;
    }

    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

std::vector<uint8_t> zlibStoreNoCompression(const uint8_t* data, size_t len) {
    std::vector<uint8_t> out;
    if (!data || len == 0) {
        return out;
    }

    out.reserve(len + (len / 65535 + 1) * 5 + 6);

    // ZLIB header for no/fast compression with 32K window.
    out.push_back(0x78);
    out.push_back(0x01);

    size_t offset = 0;
    while (offset < len) {
        const size_t chunk = std::min<size_t>(65535, len - offset);
        const bool finalBlock = (offset + chunk) >= len;
        const uint16_t l = static_cast<uint16_t>(chunk);
        const uint16_t nl = static_cast<uint16_t>(~l);

        // BFINAL + BTYPE=00 (stored block), byte-aligned.
        out.push_back(finalBlock ? 0x01 : 0x00);
        out.push_back(static_cast<uint8_t>(l & 0xFF));
        out.push_back(static_cast<uint8_t>((l >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(nl & 0xFF));
        out.push_back(static_cast<uint8_t>((nl >> 8) & 0xFF));
        out.insert(out.end(), data + offset, data + offset + chunk);
        offset += chunk;
    }

    const uint32_t adler = adler32Compute(data, len);
    out.push_back(static_cast<uint8_t>((adler >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((adler >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((adler >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(adler & 0xFF));
    return out;
}

void writeBE32(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
}

void writeChunk(std::vector<uint8_t>& out, const char type[4],
                const uint8_t* data, uint32_t len) {
    writeBE32(out, len);
    size_t typeStart = out.size();
    out.insert(out.end(), type, type + 4);
    if (data && len > 0) {
        out.insert(out.end(), data, data + len);
    }
    const uint32_t crc = crc32Compute(out.data() + typeStart, 4 + len);
    writeBE32(out, crc);
}

std::vector<uint8_t> encodePngFromBgra(const uint8_t* pixels, int width, int height, int stride) {
    std::vector<uint8_t> result;
    if (!pixels || width <= 0 || height <= 0 || stride <= 0) {
        return result;
    }

    const size_t rawSize = static_cast<size_t>(height) * (1 + static_cast<size_t>(width) * 3);
    std::vector<uint8_t> raw(rawSize);
    size_t pos = 0;
    for (int y = 0; y < height; ++y) {
        raw[pos++] = 0;
        const auto* row = pixels + static_cast<size_t>(y) * static_cast<size_t>(stride);
        for (int x = 0; x < width; ++x) {
            const auto* px = row + static_cast<size_t>(x) * 4;
            raw[pos++] = px[2];
            raw[pos++] = px[1];
            raw[pos++] = px[0];
        }
    }

    std::vector<uint8_t> compressed = zlibStoreNoCompression(raw.data(), rawSize);
    if (compressed.empty()) {
        std::cerr << "PNG encode failed" << std::endl;
        return result;
    }

    const uint8_t sig[] = {137, 80, 78, 71, 13, 10, 26, 10};
    result.insert(result.end(), sig, sig + 8);

    uint8_t ihdr[13];
    ihdr[0] = (width >> 24) & 0xFF;
    ihdr[1] = (width >> 16) & 0xFF;
    ihdr[2] = (width >> 8) & 0xFF;
    ihdr[3] = width & 0xFF;
    ihdr[4] = (height >> 24) & 0xFF;
    ihdr[5] = (height >> 16) & 0xFF;
    ihdr[6] = (height >> 8) & 0xFF;
    ihdr[7] = height & 0xFF;
    ihdr[8] = 8;
    ihdr[9] = 2;
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
    writeChunk(result, "IHDR", ihdr, 13);
    writeChunk(result, "IDAT", compressed.data(), static_cast<uint32_t>(compressed.size()));
    writeChunk(result, "IEND", nullptr, 0);
    return result;
}

void writeFile(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return;
    }
    file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

std::string trimRight(std::string value) {
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
        value.pop_back();
    }
    return value;
}

std::string sanitizeFilename(std::string value) {
    static constexpr char invalidChars[] = {'<', '>', ':', '"', '/', '\\', '|', '?', '*'};
    for (char& ch : value) {
        if (std::find(std::begin(invalidChars), std::end(invalidChars), ch) != std::end(invalidChars)) {
            ch = '_';
        }
    }
    if (value.empty()) {
        return "window";
    }
    return value;
}

} // namespace

WinScreenCapture::WinScreenCapture(HWND target)
    : target_(target) {}

platform::CaptureFrame WinScreenCapture::capture() {
    platform::CaptureFrame frame;
    if (!IsWindow(target_)) {
        std::cerr << "Invalid target HWND for capture" << std::endl;
        return frame;
    }

    RECT rect{};
    if (!GetWindowRect(target_, &rect)) {
        std::cerr << "GetWindowRect failed: " << GetLastError() << std::endl;
        return frame;
    }

    const int width = static_cast<int>(std::max<LONG>(1, rect.right - rect.left));
    const int height = static_cast<int>(std::max<LONG>(1, rect.bottom - rect.top));

    HDC screenDc = GetDC(nullptr);
    if (!screenDc) {
        return frame;
    }
    HDC memDc = CreateCompatibleDC(screenDc);
    if (!memDc) {
        ReleaseDC(nullptr, screenDc);
        return frame;
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screenDc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib || !bits) {
        DeleteDC(memDc);
        ReleaseDC(nullptr, screenDc);
        return frame;
    }

    HGDIOBJ old = SelectObject(memDc, dib);
    BOOL copied = BitBlt(
        memDc,
        0,
        0,
        width,
        height,
        screenDc,
        rect.left,
        rect.top,
        SRCCOPY | CAPTUREBLT);
    if (!copied) {
        copied = PrintWindow(target_, memDc, PW_RENDERFULLCONTENT);
    }

    if (copied) {
        frame.png = encodePngFromBgra(
            static_cast<const uint8_t*>(bits),
            width,
            height,
            width * 4);
        frame.sourceWidth = width;
        frame.sourceHeight = height;
    }

    SelectObject(memDc, old);
    DeleteObject(dib);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);
    return frame;
}

void WinScreenCapture::takeScreenshot(const std::string& dir, const std::string& name) {
    auto frame = capture();
    if (frame.png.empty()) {
        return;
    }

    std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _MSC_VER
    localtime_s(&local, &now);
#else
    local = *std::localtime(&now);
#endif

    char dateStr[32];
    std::strftime(dateStr, sizeof(dateStr), "%y%m%d-%H%M", &local);

    const std::string fileName = dir + "/screen_" + sanitizeFilename(trimRight(name)) + "_" + dateStr + ".png";
    writeFile(fileName, frame.png);
    std::cout << "Screenshot saved: " << fileName << std::endl;
}
