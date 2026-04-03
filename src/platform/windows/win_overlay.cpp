#include "platform/windows/win_overlay.h"

#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>

namespace {

#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000
#endif

std::string toUtf8(const std::wstring& value) {
    if (value.empty()) {
        return std::string();
    }

    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return std::string();
    }

    std::string utf8(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, utf8.data(), size, nullptr, nullptr);
    if (!utf8.empty()) {
        utf8.pop_back();
    }
    return utf8;
}

constexpr int kCurveSegments = 15;
constexpr UINT kHotkeyModifiers = MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT;
constexpr int kHotkeyQ = 110;

void registerHotkeys(HWND hwnd) {
    if (!RegisterHotKey(hwnd, kHotkeyQ, kHotkeyModifiers, 'Q')) {
        std::cerr << "Warning: failed to register quit hotkey CTRL+SHIFT+Q"
                  << " (error " << GetLastError() << ")" << std::endl;
    }
}

void unregisterHotkeys(HWND hwnd) {
    UnregisterHotKey(hwnd, kHotkeyQ);
}

} // namespace

WinOverlay::WinOverlay(HWND target)
    : target_(target) {}

WinOverlay::~WinOverlay() {
    if (uiThread_.joinable() && initOk_) {
        OverlayCommand shutdown;
        shutdown.type = OverlayCommand::Type::Shutdown;
        enqueueCommand(std::move(shutdown), true);
    }
    if (uiThread_.joinable()) {
        uiThread_.join();
    }
}

bool WinOverlay::init() {
    if (!IsWindow(target_)) {
        std::cerr << "Invalid target HWND" << std::endl;
        return false;
    }

    wchar_t title[256]{};
    GetWindowTextW(target_, title, 256);
    targetName_ = toUtf8(title);
    if (targetName_.empty()) {
        targetName_ = "window";
    }

    uiThread_ = std::thread([this]() { runUiThread(); });
    waitForInit();
    return initOk_;
}

int WinOverlay::getEventFd() const {
    return -1;
}

std::vector<platform::OverlayAction> WinOverlay::pollActions() {
    std::scoped_lock lock(mutex_);
    auto actions = std::move(pendingActions_);
    pendingActions_.clear();
    return actions;
}

void WinOverlay::activate() {
    OverlayCommand command;
    command.type = OverlayCommand::Type::Activate;
    enqueueCommand(std::move(command), true);
}

void WinOverlay::deactivate() {
    OverlayCommand command;
    command.type = OverlayCommand::Type::Deactivate;
    enqueueCommand(std::move(command), true);
}

bool WinOverlay::isActive() const {
    std::scoped_lock lock(mutex_);
    return active_;
}

void WinOverlay::drawStroke(const std::vector<platform::StrokePoint>& points) {
    OverlayCommand command;
    command.type = OverlayCommand::Type::Draw;
    command.stroke = points;
    enqueueCommand(std::move(command), false);
}

void WinOverlay::clear() {
    OverlayCommand command;
    command.type = OverlayCommand::Type::Clear;
    enqueueCommand(std::move(command), true);
}

void WinOverlay::redraw(const std::vector<std::vector<platform::StrokePoint>>& history) {
    OverlayCommand command;
    command.type = OverlayCommand::Type::Redraw;
    command.history = history;
    enqueueCommand(std::move(command), true);
}

std::string WinOverlay::targetName() const {
    return targetName_;
}

void WinOverlay::enqueueCommand(OverlayCommand command, bool wait) {
    std::shared_ptr<PendingSignal> signal;
    if (wait) {
        signal = std::make_shared<PendingSignal>();
        command.signal = signal;
    }

    bool canWait = wait;
    {
        std::scoped_lock lock(mutex_);
        if (shuttingDown_ && command.type != OverlayCommand::Type::Shutdown) {
            return;
        }
        commandQueue_.push(std::move(command));
        if (hwnd_) {
            if (!PostMessageW(hwnd_, kMsgProcessCommands, 0, 0)) {
                canWait = false;
            }
        } else {
            canWait = false;
        }
    }

    if (!wait || !canWait) {
        return;
    }

    if (!signal) {
        return;
    }
    std::unique_lock lock(signal->mutex);
    signal->cv.wait(lock, [&signal]() { return signal->done; });
}

void WinOverlay::notifyCommandDone(const std::shared_ptr<PendingSignal>& signal) {
    if (!signal) {
        return;
    }
    {
        std::scoped_lock lock(signal->mutex);
        signal->done = true;
    }
    signal->cv.notify_all();
}

void WinOverlay::processQueuedCommands() {
    std::queue<OverlayCommand> commands;
    {
        std::scoped_lock lock(mutex_);
        std::swap(commands, commandQueue_);
    }

    while (!commands.empty()) {
        auto command = std::move(commands.front());
        commands.pop();

        switch (command.type) {
        case OverlayCommand::Type::Activate:
            activateOnUiThread();
            break;
        case OverlayCommand::Type::Deactivate:
            deactivateOnUiThread();
            break;
        case OverlayCommand::Type::Draw:
            drawStrokeOnUiThread(command.stroke);
            break;
        case OverlayCommand::Type::Clear:
            clearOnUiThread();
            break;
        case OverlayCommand::Type::Redraw:
            redrawOnUiThread(command.history);
            break;
        case OverlayCommand::Type::Shutdown:
            {
                std::scoped_lock lock(mutex_);
                shuttingDown_ = true;
            }
            PostQuitMessage(0);
            break;
        }

        notifyCommandDone(command.signal);
    }
}

void WinOverlay::runUiThread() {
    if (!initWindowOnUiThread()) {
        cleanupUiThread();
        setInitResult(false);
        return;
    }

    setInitResult(true);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    cleanupUiThread();
}

bool WinOverlay::initWindowOnUiThread() {
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    if (Gdiplus::GdiplusStartup(&gdiplusToken_, &gdiplusStartupInput, nullptr) != Gdiplus::Ok) {
        std::cerr << "GDI+ startup failed" << std::endl;
        return false;
    }

    const wchar_t* className = L"RemotePadOverlayWindow";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WinOverlay::windowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = className;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    if (!RegisterClassExW(&wc)) {
        const DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            std::cerr << "RegisterClassEx failed: " << error << std::endl;
            return false;
        }
    }

    refreshTargetRect();
    hwnd_ = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        className,
        L"REMOTEPAD",
        WS_POPUP,
        x_,
        y_,
        width_,
        height_,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        this);
    if (!hwnd_) {
        std::cerr << "CreateWindowEx failed: " << GetLastError() << std::endl;
        return false;
    }

    registerHotkeys(hwnd_);
    if (!createBitmapSurface()) {
        return false;
    }

    clearOnUiThread();
    return true;
}

void WinOverlay::cleanupUiThread() {
    if (hwnd_) {
        unregisterHotkeys(hwnd_);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }

    destroyBitmapSurface();

    if (gdiplusToken_ != 0) {
        Gdiplus::GdiplusShutdown(gdiplusToken_);
        gdiplusToken_ = 0;
    }
}

void WinOverlay::setInitResult(bool ok) {
    {
        std::scoped_lock lock(mutex_);
        initOk_ = ok;
        initDone_ = true;
    }
    initCv_.notify_all();
}

void WinOverlay::waitForInit() {
    std::unique_lock lock(mutex_);
    initCv_.wait(lock, [this]() { return initDone_; });
}

LRESULT CALLBACK WinOverlay::windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    WinOverlay* self = reinterpret_cast<WinOverlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<WinOverlay*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return TRUE;
    }
    if (!self) {
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    switch (msg) {
    case kMsgProcessCommands:
        self->processQueuedCommands();
        return 0;
    case WM_HOTKEY:
        self->handleHotkey(static_cast<int>(wParam));
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void WinOverlay::handleHotkey(int hotkeyId) {
    if (hotkeyId == kHotkeyQ) {
        pushAction(platform::OverlayAction::Terminate);
    }
}

void WinOverlay::pushAction(platform::OverlayAction action) {
    std::scoped_lock lock(mutex_);
    pendingActions_.push_back(action);
}

void WinOverlay::activateOnUiThread() {
    refreshTargetRect();
    SetWindowPos(hwnd_, HWND_TOPMOST, x_, y_, width_, height_, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    {
        std::scoped_lock lock(mutex_);
        active_ = true;
    }
    updateLayeredWindowContent();
}

void WinOverlay::deactivateOnUiThread() {
    ShowWindow(hwnd_, SW_HIDE);
    std::scoped_lock lock(mutex_);
    active_ = false;
}

void WinOverlay::clearOnUiThread() {
    if (dibBits_ && width_ > 0 && height_ > 0) {
        std::memset(dibBits_, 0, static_cast<size_t>(width_) * static_cast<size_t>(height_) * 4);
    }
    if (isActive()) {
        updateLayeredWindowContent();
    }
}

void WinOverlay::redrawOnUiThread(const std::vector<std::vector<platform::StrokePoint>>& history) {
    clearOnUiThread();
    for (const auto& stroke : history) {
        std::vector<platform::StrokePoint> points;
        points.reserve(stroke.size());
        for (const auto& point : stroke) {
            points.push_back(point);
            drawSegment(points);
        }
    }
    if (isActive()) {
        updateLayeredWindowContent();
    }
}

void WinOverlay::drawStrokeOnUiThread(const std::vector<platform::StrokePoint>& points) {
    drawSegment(points);
    if (isActive()) {
        updateLayeredWindowContent();
    }
}

void WinOverlay::drawSegment(const std::vector<platform::StrokePoint>& points) {
    int l = static_cast<int>(points.size()) - 1;
    if (l < 3 || !memDc_) {
        return;
    }

    const int lineWidth = std::max(1, static_cast<int>(points[l - 1].lineWidth));
    const uint32_t color = points[l - 1].color;

    Gdiplus::Graphics graphics(memDc_);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);

    Gdiplus::Color penColor(
        255,
        static_cast<BYTE>((color >> 16) & 0xFF),
        static_cast<BYTE>((color >> 8) & 0xFF),
        static_cast<BYTE>(color & 0xFF));
    Gdiplus::Pen pen(penColor, static_cast<Gdiplus::REAL>(lineWidth));
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);

    double currentX = points[l].x;
    double currentY = points[l].y;
    const double xc = points[l - 1].x;
    const double yc = points[l - 1].y;
    const double cpx = (currentX + xc) / 2.0;
    const double cpy = (currentY + yc) / 2.0;

    for (int i = 0; i <= kCurveSegments; ++i) {
        const double t = static_cast<double>(i) / kCurveSegments;
        const double u = 1.0 - t;
        const double pointX = u * u * currentX + 2.0 * u * t * cpx + t * t * xc;
        const double pointY = u * u * currentY + 2.0 * u * t * cpy + t * t * yc;
        graphics.DrawLine(
            &pen,
            static_cast<Gdiplus::REAL>(currentX),
            static_cast<Gdiplus::REAL>(currentY),
            static_cast<Gdiplus::REAL>(pointX),
            static_cast<Gdiplus::REAL>(pointY));
        currentX = pointX;
        currentY = pointY;
    }
}

void WinOverlay::updateLayeredWindowContent() {
    if (!hwnd_ || !memDc_ || width_ <= 0 || height_ <= 0) {
        return;
    }

    HDC screenDc = GetDC(nullptr);
    if (!screenDc) {
        return;
    }

    POINT srcPoint{0, 0};
    POINT dstPoint{x_, y_};
    SIZE size{width_, height_};
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    UpdateLayeredWindow(
        hwnd_,
        screenDc,
        &dstPoint,
        &size,
        memDc_,
        &srcPoint,
        0,
        &blend,
        ULW_ALPHA);
    ReleaseDC(nullptr, screenDc);
}

void WinOverlay::refreshTargetRect() {
    RECT rect{};
    if (!GetWindowRect(target_, &rect)) {
        return;
    }

    x_ = rect.left;
    y_ = rect.top;
    width_ = static_cast<int>(std::max<LONG>(1, rect.right - rect.left));
    height_ = static_cast<int>(std::max<LONG>(1, rect.bottom - rect.top));
}

bool WinOverlay::createBitmapSurface() {
    HDC screenDc = GetDC(nullptr);
    if (!screenDc) {
        return false;
    }

    memDc_ = CreateCompatibleDC(screenDc);
    if (!memDc_) {
        ReleaseDC(nullptr, screenDc);
        return false;
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width_;
    bmi.bmiHeader.biHeight = -height_;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    dibBitmap_ = CreateDIBSection(screenDc, &bmi, DIB_RGB_COLORS, &dibBits_, nullptr, 0);
    ReleaseDC(nullptr, screenDc);

    if (!dibBitmap_ || !dibBits_) {
        destroyBitmapSurface();
        return false;
    }

    oldBitmap_ = SelectObject(memDc_, dibBitmap_);
    clearOnUiThread();
    return true;
}

void WinOverlay::destroyBitmapSurface() {
    if (memDc_ && oldBitmap_) {
        SelectObject(memDc_, oldBitmap_);
        oldBitmap_ = nullptr;
    }
    if (dibBitmap_) {
        DeleteObject(dibBitmap_);
        dibBitmap_ = nullptr;
    }
    if (memDc_) {
        DeleteDC(memDc_);
        memDc_ = nullptr;
    }
    dibBits_ = nullptr;
}
