#include "x_screen.h"

#include <X11/Xutil.h>
#include <zlib.h>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>

namespace xscreen {

namespace {

int componentShift(unsigned long mask) {
    int shift = 0;
    while (mask != 0 && (mask & 1UL) == 0) {
        mask >>= 1;
        shift++;
    }
    return shift;
}

int componentBits(unsigned long mask) {
    int bits = 0;
    while (mask != 0) {
        bits += static_cast<int>(mask & 1UL);
        mask >>= 1;
    }
    return bits;
}

uint8_t expandComponent(unsigned long value, int bits) {
    if (bits <= 0) return 0;
    if (bits >= 8) return static_cast<uint8_t>(value >> (bits - 8));
    return static_cast<uint8_t>((value * 255UL) / ((1UL << bits) - 1UL));
}

unsigned long readPixel(const XImage* img, int x, int y) {
    const auto* row = reinterpret_cast<const unsigned char*>(
        img->data + static_cast<long>(y) * img->bytes_per_line);
    const auto* pixel = row + static_cast<long>(x) * (img->bits_per_pixel / 8);

    switch (img->bits_per_pixel) {
    case 32:
        if (img->byte_order == LSBFirst) {
            return static_cast<unsigned long>(pixel[0]) |
                   (static_cast<unsigned long>(pixel[1]) << 8) |
                   (static_cast<unsigned long>(pixel[2]) << 16) |
                   (static_cast<unsigned long>(pixel[3]) << 24);
        }
        return static_cast<unsigned long>(pixel[3]) |
               (static_cast<unsigned long>(pixel[2]) << 8) |
               (static_cast<unsigned long>(pixel[1]) << 16) |
               (static_cast<unsigned long>(pixel[0]) << 24);
    case 24:
        if (img->byte_order == LSBFirst) {
            return static_cast<unsigned long>(pixel[0]) |
                   (static_cast<unsigned long>(pixel[1]) << 8) |
                   (static_cast<unsigned long>(pixel[2]) << 16);
        }
        return static_cast<unsigned long>(pixel[2]) |
               (static_cast<unsigned long>(pixel[1]) << 8) |
               (static_cast<unsigned long>(pixel[0]) << 16);
    case 16:
        if (img->byte_order == LSBFirst) {
            return static_cast<unsigned long>(pixel[0]) |
                   (static_cast<unsigned long>(pixel[1]) << 8);
        }
        return static_cast<unsigned long>(pixel[1]) |
               (static_cast<unsigned long>(pixel[0]) << 8);
    case 8:
        return pixel[0];
    default:
        return XGetPixel(const_cast<XImage*>(img), x, y);
    }
}

// Write a big-endian 32-bit value into a buffer
void writeBE32(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
}

// Append a PNG chunk: length + type + data + CRC
void writeChunk(std::vector<uint8_t>& out, const char type[4],
                const uint8_t* data, uint32_t len) {
    writeBE32(out, len);
    size_t typeStart = out.size();
    out.insert(out.end(), type, type + 4);
    if (data && len > 0) {
        out.insert(out.end(), data, data + len);
    }
    uint32_t crc = crc32(0, out.data() + typeStart, 4 + len);
    writeBE32(out, crc);
}

std::vector<uint8_t> encodePng(XImage* img, int width, int height) {
    std::vector<uint8_t> result;
    if (!img || width <= 0 || height <= 0) return result;

    const int redShift = componentShift(img->red_mask);
    const int greenShift = componentShift(img->green_mask);
    const int blueShift = componentShift(img->blue_mask);
    const int redBits = componentBits(img->red_mask);
    const int greenBits = componentBits(img->green_mask);
    const int blueBits = componentBits(img->blue_mask);

    // Build raw image data: filter byte (0) + RGB for each row
    size_t rawSize = static_cast<size_t>(height) * (1 + static_cast<size_t>(width) * 3);
    std::vector<uint8_t> raw(rawSize);
    size_t pos = 0;
    for (int y = 0; y < height; y++) {
        raw[pos++] = 0; // filter: None
        for (int x = 0; x < width; x++) {
            unsigned long px = readPixel(img, x, y);
            raw[pos++] = expandComponent((px & img->red_mask) >> redShift, redBits);
            raw[pos++] = expandComponent((px & img->green_mask) >> greenShift, greenBits);
            raw[pos++] = expandComponent((px & img->blue_mask) >> blueShift, blueBits);
        }
    }

    // Deflate the raw data
    uLongf compBound = compressBound(rawSize);
    std::vector<uint8_t> compressed(compBound);
    int zret = compress2(compressed.data(), &compBound, raw.data(), rawSize, 6);
    if (zret != Z_OK) {
        std::cerr << "PNG zlib compress failed: " << zret << std::endl;
        return result;
    }
    compressed.resize(compBound);

    // PNG signature
    const uint8_t sig[] = {137, 80, 78, 71, 13, 10, 26, 10};
    result.insert(result.end(), sig, sig + 8);

    // IHDR chunk
    uint8_t ihdr[13];
    ihdr[0] = (width >> 24) & 0xFF;
    ihdr[1] = (width >> 16) & 0xFF;
    ihdr[2] = (width >> 8) & 0xFF;
    ihdr[3] = width & 0xFF;
    ihdr[4] = (height >> 24) & 0xFF;
    ihdr[5] = (height >> 16) & 0xFF;
    ihdr[6] = (height >> 8) & 0xFF;
    ihdr[7] = height & 0xFF;
    ihdr[8] = 8;  // bit depth
    ihdr[9] = 2;  // color type: RGB
    ihdr[10] = 0; // compression
    ihdr[11] = 0; // filter
    ihdr[12] = 0; // interlace
    writeChunk(result, "IHDR", ihdr, 13);

    // IDAT chunk (compressed image data)
    writeChunk(result, "IDAT", compressed.data(), static_cast<uint32_t>(compressed.size()));

    // IEND chunk
    writeChunk(result, "IEND", nullptr, 0);

    return result;
}

void writeFile(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream file(path, std::ios::binary);
    if (file) {
        file.write(reinterpret_cast<const char*>(data.data()),
                   static_cast<std::streamsize>(data.size()));
    }
}

} // namespace

CaptureFrame getBufferedScreen(Display* dpy, Window root, Window wid) {
    CaptureFrame frame;
    XWindowAttributes attr{};
    if (!XGetWindowAttributes(dpy, wid, &attr)) {
        std::cerr << "Failed to get window attributes" << std::endl;
        return frame;
    }

    int destX = 0, destY = 0;
    Window child;
    XTranslateCoordinates(dpy, wid, root, 0, 0, &destX, &destY, &child);

    XImage* img = XGetImage(dpy, root, destX, destY,
                            static_cast<unsigned int>(attr.width),
                            static_cast<unsigned int>(attr.height),
                            AllPlanes, ZPixmap);
    if (!img) {
        std::cerr << "Failed to capture screen" << std::endl;
        return frame;
    }

    frame.png = encodePng(img, attr.width, attr.height);
    frame.sourceWidth = attr.width;
    frame.sourceHeight = attr.height;
    XDestroyImage(img);
    return frame;
}

void takeScreenshot(Display* dpy, Window root, Window wid,
                    const std::string& dir, const std::string& name) {
    auto frame = getBufferedScreen(dpy, root, wid);
    if (frame.png.empty()) return;

    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    char dateStr[32];
    strftime(dateStr, sizeof(dateStr), "%y%m%d-%H%M", t);

    std::string trimmedName = name;
    while (!trimmedName.empty() && trimmedName.back() == ' ')
        trimmedName.pop_back();

    std::string filename = dir + "/screen_" + trimmedName + "_" + dateStr + ".png";
    writeFile(filename, frame.png);
    std::cout << "Screenshot saved: " << filename << std::endl;
}

} // namespace xscreen
