#pragma once

#include "platform/overlay.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

class WinOverlay : public platform::IOverlay {
public:
    explicit WinOverlay(HWND target);
    ~WinOverlay() override;

    bool init() override;
    int getEventFd() const override;
    std::vector<platform::OverlayAction> pollActions() override;

    void activate() override;
    void deactivate() override;
    bool isActive() const override;

    void drawStroke(const std::vector<platform::StrokePoint>& points) override;
    void clear() override;
    void redraw(const std::vector<std::vector<platform::StrokePoint>>& history) override;

    std::string targetName() const override;

private:
    struct PendingSignal {
        std::mutex mutex;
        std::condition_variable cv;
        bool done = false;
    };

    struct OverlayCommand {
        enum class Type {
            Activate,
            Deactivate,
            Draw,
            Clear,
            Redraw,
            Shutdown,
        };
        Type type = Type::Draw;
        std::vector<platform::StrokePoint> stroke;
        std::vector<std::vector<platform::StrokePoint>> history;
        std::shared_ptr<PendingSignal> signal;
    };

    static constexpr UINT kMsgProcessCommands = WM_APP + 1;

    void enqueueCommand(OverlayCommand command, bool wait);
    void notifyCommandDone(const std::shared_ptr<PendingSignal>& signal);
    void processQueuedCommands();

    void runUiThread();
    bool initWindowOnUiThread();
    void cleanupUiThread();
    void setInitResult(bool ok);
    void waitForInit();

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void handleHotkey(int hotkeyId);
    void pushAction(platform::OverlayAction action);

    void activateOnUiThread();
    void deactivateOnUiThread();
    void clearOnUiThread();
    void redrawOnUiThread(const std::vector<std::vector<platform::StrokePoint>>& history);
    void drawStrokeOnUiThread(const std::vector<platform::StrokePoint>& points);
    void drawSegment(const std::vector<platform::StrokePoint>& points);
    void updateLayeredWindowContent();
    void refreshTargetRect();
    bool createBitmapSurface();
    void destroyBitmapSurface();

    HWND target_ = nullptr;
    std::string targetName_;

    std::thread uiThread_;
    mutable std::mutex mutex_;
    std::condition_variable initCv_;
    bool initDone_ = false;
    bool initOk_ = false;
    bool shuttingDown_ = false;
    bool active_ = false;

    HWND hwnd_ = nullptr;
    HDC memDc_ = nullptr;
    HBITMAP dibBitmap_ = nullptr;
    HGDIOBJ oldBitmap_ = nullptr;
    void* dibBits_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    int x_ = 0;
    int y_ = 0;

    std::queue<OverlayCommand> commandQueue_;
    std::vector<platform::OverlayAction> pendingActions_;

    ULONG_PTR gdiplusToken_ = 0;
};
