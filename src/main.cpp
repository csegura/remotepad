#include "app_config.h"
#include "remote_pad.h"
#include "web_server.h"

#ifndef _WIN32
#include "platform/linux/linux_overlay.h"
#include "platform/linux/linux_screen_capture.h"
#include "x_tools.h"

#include <X11/Xlib.h>
#include <arpa/inet.h>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ifaddrs.h>
#include <iostream>
#include <memory>
#include <net/if.h>
#include <netinet/in.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;

static const char* PIDFILE = "/tmp/remotepad.pid";

static WebServer* g_server = nullptr;

static void signalHandler(int /*sig*/) {
    if (g_server) {
        g_server->stop();
    }
}

static void writePidFile() {
    std::ofstream f(PIDFILE);
    if (f) {
        f << getpid() << std::endl;
    }
}

static void removePidFile() {
    std::error_code ec;
    fs::remove(PIDFILE, ec);
}

static int doStop() {
    std::ifstream f(PIDFILE);
    if (!f) {
        std::cerr << "No running instance found (no pid file)" << std::endl;
        return 1;
    }

    pid_t pid = 0;
    f >> pid;
    if (pid <= 0) {
        std::cerr << "Invalid pid file" << std::endl;
        std::error_code ec;
        fs::remove(PIDFILE, ec);
        return 1;
    }

    if (kill(pid, 0) != 0) {
        std::cerr << "Process " << pid << " not running (stale pid file removed)" << std::endl;
        std::error_code ec;
        fs::remove(PIDFILE, ec);
        return 1;
    }

    kill(pid, SIGTERM);
    std::cout << "remotepad stopped (pid " << pid << ")" << std::endl;
    return 0;
}

static void showHelp(const char* prog) {
    std::cout
        << "RemotePad v" << REMOTEPAD_VERSION << "\n"
        << REMOTEPAD_URL << "\n"
        << "\n"
        << "Usage: " << prog << " [current | list | stop | <window-id> | help] [-p port]\n"
        << "\n"
        << "Commands:\n"
        << "  (no args)     Select window by clicking\n"
        << "  current       Use current focused window\n"
        << "  list          List selectable windows and exit\n"
        << "  stop          Stop running remotepad instance\n"
        << "  <window-id>   Start overlay on the given X11 window\n"
        << "  help          Show this help\n"
        << "\n"
        << "Options:\n"
        << "  -p <port>     Server port (default: 50005)\n"
        << "\n"
        << "Tip: pass window ID as decimal or hex (e.g. 12345678 or 0xBC614E).\n"
        << "\n"
        << "Quit hotkey: CTRL+SHIFT+Q\n";
}

static int parsePort(const std::string& raw) {
    size_t parsed = 0;
    int value = std::stoi(raw, &parsed);
    if (parsed != raw.size() || value < 1 || value > 65535) {
        throw std::invalid_argument("Invalid port");
    }
    return value;
}

static int findPortArg(int argc, char* argv[]) {
    for (int i = 1; i < argc - 1; ++i) {
        if (std::string(argv[i]) == "-p") {
            return parsePort(argv[i + 1]);
        }
    }
    return 0; // not specified
}

static Window parseWindowId(const std::string& raw) {
    size_t parsed = 0;
    auto value = std::stoull(raw, &parsed, 0);
    if (parsed != raw.size()) {
        throw std::invalid_argument("Invalid window id");
    }
    return static_cast<Window>(value);
}

static std::string getExecutablePath() {
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) return "";
    buf[len] = '\0';
    return std::string(buf);
}

static bool spawnBackground(Window target, int port, pid_t& pidOut) {
    std::string exePath = getExecutablePath();
    if (exePath.empty()) return false;

    std::string widStr = std::to_string(target);
    std::string portStr = std::to_string(port);

    pid_t pid = fork();
    if (pid < 0) return false;

    if (pid == 0) {
        // Child: detach and exec
        setsid();
        close(STDIN_FILENO);

        // Redirect stdout/stderr to log
        std::string logPath = "/tmp/remotepad.log";
        FILE* logFile = fopen(logPath.c_str(), "w");
        if (logFile) {
            dup2(fileno(logFile), STDOUT_FILENO);
            dup2(fileno(logFile), STDERR_FILENO);
            fclose(logFile);
        }

        execl(exePath.c_str(), exePath.c_str(), "--run", widStr.c_str(),
              "-p", portStr.c_str(), nullptr);
        _exit(1);
    }

    pidOut = pid;
    return true;
}

static std::string formatWindowId(Window wid) {
    std::ostringstream ss;
    ss << "0x" << std::hex << std::uppercase << wid;
    return ss.str();
}

static void printWindowList(const std::vector<xtools::WindowEntry>& windows) {
    std::cout << "Selectable windows:\n";
    for (size_t i = 0; i < windows.size(); ++i) {
        std::cout << "  [" << i << "] "
                  << formatWindowId(windows[i].wid)
                  << "  " << windows[i].name << '\n';
    }
}

static int runServerForeground(Window target, int portOverride) {
    AppConfig config = loadConfig();
    if (portOverride > 0) config.port = portOverride;

    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        std::cerr << "Cannot open X display" << std::endl;
        return 1;
    }

    auto overlay = std::make_unique<LinuxOverlay>(dpy, target);
    auto capture = std::make_unique<LinuxScreenCapture>(dpy, DefaultRootWindow(dpy), target);
    RemotePad pad(std::move(overlay), std::move(capture), config);
    if (!pad.init()) {
        std::cerr << "Failed to initialize RemotePad" << std::endl;
        XCloseDisplay(dpy);
        return 1;
    }

    std::string staticDir = "./client";

    WebServer server(config.port, staticDir, pad);
    g_server = &server;

    writePidFile();
    std::atexit(removePidFile);

    struct sigaction sa{};
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);

    server.run();

    pad.drawClear();
    pad.deactivateOverlay();

    g_server = nullptr;
    XCloseDisplay(dpy);
    return 0;
}

int main(int argc, char* argv[]) {
    int portOverride = findPortArg(argc, argv);

    // Handle --run (internal mode for background child), help, stop
    if (argc >= 2) {
        const std::string cmd(argv[1]);

        if (cmd == "help" || cmd == "--help" || cmd == "-h") {
            showHelp(argv[0]);
            return 0;
        }
        if (cmd == "stop") {
            return doStop();
        }
        if (cmd == "--run") {
            if (argc < 3) {
                std::cerr << "Missing window ID for internal --run mode" << std::endl;
                return 1;
            }
            Window target = None;
            try {
                target = parseWindowId(argv[2]);
            } catch (...) {
                std::cerr << "Invalid window ID for --run mode: " << argv[2] << std::endl;
                return 1;
            }
            return runServerForeground(target, portOverride);
        }
    }

    // Check for existing instance
    {
        std::ifstream f(PIDFILE);
        if (f) {
            pid_t pid = 0;
            f >> pid;
            if (pid > 0 && kill(pid, 0) == 0) {
                std::cerr << "remotepad already running (pid " << pid << ")" << std::endl;
                std::cerr << "Run: remotepad stop" << std::endl;
                return 1;
            }
        }
    }

    std::cout << "RemotePad v" << REMOTEPAD_VERSION << std::endl;
    std::cout << REMOTEPAD_URL << std::endl;
    std::cout << std::endl;

    // Resolve target window
    Window target = None;

    // Find the first non-flag argument (skip -p <port>)
    std::string cmd;
    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "-p") { ++i; continue; }
        cmd = arg;
        break;
    }

    if (cmd.empty()) {
        // Interactive click selection
        Display* dpy = XOpenDisplay(nullptr);
        if (!dpy) {
            std::cerr << "Cannot open X display" << std::endl;
            return 1;
        }
        std::cout << "Click on a window to select it (ESC to cancel)..." << std::endl;
        target = xtools::selectWindowByClick(dpy);
        XCloseDisplay(dpy);
    } else if (cmd == "list") {
        Display* dpy = XOpenDisplay(nullptr);
        if (!dpy) {
            std::cerr << "Cannot open X display" << std::endl;
            return 1;
        }
        auto windows = xtools::collectTopLevelWindows(dpy);
        XCloseDisplay(dpy);
        if (windows.empty()) {
            std::cerr << "No selectable top-level windows found." << std::endl;
            return 1;
        }
        printWindowList(windows);
        return 0;
    } else if (cmd == "current") {
        Display* dpy = XOpenDisplay(nullptr);
        if (!dpy) {
            std::cerr << "Cannot open X display" << std::endl;
            return 1;
        }
        target = xtools::getActiveWindow(dpy);
        XCloseDisplay(dpy);
    } else {
        try {
            target = parseWindowId(cmd);
        } catch (...) {
            std::cerr << "Invalid window ID: " << cmd << std::endl;
            showHelp(argv[0]);
            return 1;
        }
    }

    if (target == None) {
        std::cerr << "No window selected." << std::endl;
        return 1;
    }

    // Resolve effective port for display
    AppConfig config = loadConfig();
    if (portOverride > 0) config.port = portOverride;

    // Fork to background
    pid_t childPid = 0;
    if (!spawnBackground(target, config.port, childPid)) {
        std::cerr << "Failed to start background instance" << std::endl;
        return 1;
    }

    // Brief wait to check child started
    usleep(200000);
    if (kill(childPid, 0) != 0) {
        std::cerr << "Failed to start. Check /tmp/remotepad.log" << std::endl;
        return 1;
    }

    // Get local LAN IP (prefer 192.168/10/172.16-31, skip loopback and VPN)
    std::string ip = "localhost";
    struct ifaddrs* iflist = nullptr;
    if (getifaddrs(&iflist) == 0) {
        std::string fallback;
        for (auto* ifa = iflist; ifa; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
            if (ifa->ifa_flags & IFF_LOOPBACK) continue;
            auto* sin = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr);
            char buf[INET_ADDRSTRLEN]{};
            inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf));
            uint32_t addr = ntohl(sin->sin_addr.s_addr);
            bool isLan = (addr >> 24 == 192 && (addr >> 16 & 0xFF) == 168)
                      || (addr >> 24 == 10)
                      || (addr >> 24 == 172 && (addr >> 16 & 0xF0) == 16);
            if (isLan) {
                ip = buf;
                break;
            }
            if (fallback.empty()) fallback = buf;
        }
        if (ip == "localhost" && !fallback.empty()) ip = fallback;
        freeifaddrs(iflist);
    }

    std::cout << "remotepad running (pid " << childPid << ")" << std::endl;
    std::cout << "  Tablet: http://" << ip << ":" << config.port << std::endl;
    std::cout << "  Stop:   remotepad stop" << std::endl;
    return 0;
}

#else

#include "platform/windows/win_overlay.h"
#include "platform/windows/win_screen_capture.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

static fs::path getPidFilePath() {
    std::error_code ec;
    const fs::path tmp = fs::temp_directory_path(ec);
    if (!ec && !tmp.empty()) {
        return tmp / "remotepad-win.pid";
    }
    return fs::path("remotepad-win.pid");
}

static std::string wideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return "";
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return "";
    }
    std::string utf8(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, utf8.data(), size, nullptr, nullptr);
    utf8.pop_back();
    return utf8;
}

static std::wstring getExecutablePathW() {
    std::wstring path(MAX_PATH, L'\0');
    DWORD len = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    while (len >= path.size() - 1) {
        path.resize(path.size() * 2);
        len = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    }
    path.resize(len);
    return path;
}

static fs::path resolveStaticDir() {
    const fs::path exePath(getExecutablePathW());
    const fs::path exeDir = exePath.parent_path();

    const std::vector<fs::path> candidates = {
        fs::current_path() / "client",
        exeDir / "client",
        exeDir / ".." / "client",
        exeDir / ".." / ".." / "client",
        exeDir / ".." / ".." / ".." / "client",
    };

    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (fs::exists(candidate / "index.html", ec) && !ec) {
            const fs::path canonical = fs::weakly_canonical(candidate, ec);
            if (!ec && !canonical.empty()) {
                return canonical;
            }
            return candidate;
        }
    }

    return fs::current_path() / "client";
}

static bool isProcessRunning(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) {
        return false;
    }
    DWORD code = 0;
    const bool ok = GetExitCodeProcess(process, &code) == TRUE;
    CloseHandle(process);
    return ok && code == STILL_ACTIVE;
}

static void writePidFile() {
    std::ofstream f(getPidFilePath());
    if (f) {
        f << GetCurrentProcessId() << std::endl;
    }
}

static void removePidFile() {
    std::error_code ec;
    fs::remove(getPidFilePath(), ec);
}

static std::string getStopEventName() {
    return "remotepad_stop_event";
}

static int doStop() {
    const fs::path pidFile = getPidFilePath();
    std::ifstream f(pidFile);
    if (!f) {
        std::cerr << "No running instance found (no pid file)" << std::endl;
        return 1;
    }

    unsigned long pid = 0;
    f >> pid;
    if (pid == 0) {
        std::cerr << "Invalid pid file" << std::endl;
        std::error_code ec;
        fs::remove(pidFile, ec);
        return 1;
    }

    if (!isProcessRunning(static_cast<DWORD>(pid))) {
        std::cerr << "Process " << pid << " not running (stale pid file removed)" << std::endl;
        std::error_code ec;
        fs::remove(pidFile, ec);
        return 1;
    }

    HANDLE event = OpenEventA(EVENT_MODIFY_STATE, FALSE, getStopEventName().c_str());
    if (!event) {
        std::cerr << "Cannot signal process " << pid << " (event not found)" << std::endl;
        return 1;
    }
    SetEvent(event);
    CloseHandle(event);

    std::cout << "remotepad stopped (pid " << pid << ")" << std::endl;
    return 0;
}

struct WindowEntry {
    HWND hwnd = nullptr;
    std::string title;
    std::string className;
};

static std::vector<WindowEntry> collectSelectableWindows() {
    std::vector<WindowEntry> windows;
    EnumWindows([](HWND hwnd, LPARAM lparam) -> BOOL {
        if (!IsWindowVisible(hwnd)) {
            return TRUE;
        }
        if (GetWindow(hwnd, GW_OWNER) != nullptr) {
            return TRUE;
        }

        const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        if ((exStyle & WS_EX_TOOLWINDOW) != 0) {
            return TRUE;
        }

        wchar_t titleW[512]{};
        GetWindowTextW(hwnd, titleW, 512);
        std::wstring title(titleW);
        if (title.empty()) {
            return TRUE;
        }

        wchar_t classW[256]{};
        GetClassNameW(hwnd, classW, 256);

        auto* out = reinterpret_cast<std::vector<WindowEntry>*>(lparam);
        out->push_back(WindowEntry{
            .hwnd = hwnd,
            .title = wideToUtf8(title),
            .className = wideToUtf8(classW),
        });
        return TRUE;
    }, reinterpret_cast<LPARAM>(&windows));
    return windows;
}

static std::string formatHwnd(HWND hwnd) {
    std::ostringstream ss;
    ss << "0x" << std::hex << std::uppercase << reinterpret_cast<std::uintptr_t>(hwnd);
    return ss.str();
}

static void printWindowList(const std::vector<WindowEntry>& windows) {
    std::cout << "Selectable windows:\n";
    for (size_t i = 0; i < windows.size(); ++i) {
        std::cout
            << "  [" << i << "] "
            << formatHwnd(windows[i].hwnd)
            << "  " << windows[i].title;
        if (!windows[i].className.empty()) {
            std::cout << "  (" << windows[i].className << ")";
        }
        std::cout << '\n';
    }
}

static HWND selectWindowByCursor() {
    std::cout << "Move cursor to target window and left-click to select (ESC to cancel)..." << std::endl;

    bool wasDown = false;
    while (true) {
        if ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0) {
            return nullptr;
        }

        const bool isDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        if (isDown && !wasDown) {
            POINT pt{};
            if (!GetCursorPos(&pt)) {
                return nullptr;
            }
            HWND hwnd = WindowFromPoint(pt);
            if (!hwnd) {
                return nullptr;
            }
            hwnd = GetAncestor(hwnd, GA_ROOT);
            return hwnd;
        }
        wasDown = isDown;
        Sleep(10);
    }
}

static void showHelp(const char* prog) {
    std::cout
        << "RemotePad v" << REMOTEPAD_VERSION << "\n"
        << REMOTEPAD_URL << "\n"
        << "\n"
        << "Usage: " << prog << " [current | list | stop | <hwnd> | help] [-p port]\n"
        << "\n"
        << "Commands:\n"
        << "  (no args)     Select window by clicking with cursor\n"
        << "  current       Use current foreground window\n"
        << "  list          List selectable windows and exit\n"
        << "  stop          Stop running remotepad instance\n"
        << "  <hwnd>        Start overlay on the given target window handle\n"
        << "  help          Show this help\n"
        << "\n"
        << "Options:\n"
        << "  -p <port>     Server port (default: 50005)\n"
        << "\n"
        << "Tip: pass HWND as decimal or hex (for example 65890 or 0x10162).\n";
}

static int parsePort(const std::string& raw) {
    size_t parsed = 0;
    int value = std::stoi(raw, &parsed);
    if (parsed != raw.size() || value < 1 || value > 65535) {
        throw std::invalid_argument("Invalid port");
    }
    return value;
}

static int findPortArg(int argc, char* argv[]) {
    for (int i = 1; i < argc - 1; ++i) {
        if (std::string(argv[i]) == "-p") {
            return parsePort(argv[i + 1]);
        }
    }
    return 0;
}

static HWND parseWindowHandle(const std::string& raw) {
    size_t parsed = 0;
    const auto value = std::stoull(raw, &parsed, 0);
    if (parsed != raw.size()) {
        throw std::invalid_argument("Invalid hwnd");
    }
    return reinterpret_cast<HWND>(static_cast<std::uintptr_t>(value));
}

static bool spawnBackground(HWND target, int port, DWORD& pidOut) {
    const std::wstring exePath = getExecutablePathW();
    if (exePath.empty()) {
        return false;
    }

    std::wstringstream cmd;
    cmd << L"\"" << exePath << L"\" --run " << reinterpret_cast<std::uintptr_t>(target)
        << L" -p " << port;
    std::wstring cmdline = cmd.str();

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(
        exePath.c_str(),
        cmdline.data(),
        nullptr,
        nullptr,
        FALSE,
        DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
        nullptr,
        nullptr,
        &si,
        &pi);
    if (!ok) {
        return false;
    }

    pidOut = pi.dwProcessId;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

static int runServerForeground(HWND target, int portOverride) {
    if (!IsWindow(target)) {
        std::cerr << "Target window is invalid or not available." << std::endl;
        return 1;
    }

    AppConfig config = loadConfig();
    if (portOverride > 0) config.port = portOverride;
    auto overlay = std::make_unique<WinOverlay>(target);
    auto capture = std::make_unique<WinScreenCapture>(target);
    RemotePad pad(std::move(overlay), std::move(capture), config);
    if (!pad.init()) {
        std::cerr << "Failed to initialize RemotePad" << std::endl;
        return 1;
    }

    const fs::path staticDir = resolveStaticDir();
    std::cout << "Serving client from: " << staticDir.string() << std::endl;

    writePidFile();
    std::atexit(removePidFile);

    HANDLE stopEvent = CreateEventA(nullptr, TRUE, FALSE, getStopEventName().c_str());

    WebServer server(config.port, staticDir.string(), pad);

    std::thread stopWatcher([&server, stopEvent]() {
        if (!stopEvent) return;
        WaitForSingleObject(stopEvent, INFINITE);
        server.stop();
    });

    server.run();

    pad.drawClear();
    pad.deactivateOverlay();

    if (stopEvent) {
        SetEvent(stopEvent);
        if (stopWatcher.joinable()) stopWatcher.join();
        CloseHandle(stopEvent);
    }

    return 0;
}

int main(int argc, char* argv[]) {
#if defined(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#endif

    int portOverride = findPortArg(argc, argv);

    if (argc >= 2) {
        const std::string cmd(argv[1]);
        if (cmd == "help" || cmd == "--help" || cmd == "-h") {
            showHelp(argv[0]);
            return 0;
        }
        if (cmd == "stop") {
            return doStop();
        }
        if (cmd == "--run") {
            if (argc < 3) {
                std::cerr << "Missing HWND for internal --run mode" << std::endl;
                return 1;
            }
            HWND target = nullptr;
            try {
                target = parseWindowHandle(argv[2]);
            } catch (...) {
                std::cerr << "Invalid HWND for --run mode: " << argv[2] << std::endl;
                return 1;
            }
            return runServerForeground(target, portOverride);
        }
    }

    {
        const fs::path pidFile = getPidFilePath();
        std::ifstream f(pidFile);
        if (f) {
            unsigned long pid = 0;
            f >> pid;
            if (pid > 0 && isProcessRunning(static_cast<DWORD>(pid))) {
                std::cerr << "remotepad already running (pid " << pid << ")" << std::endl;
                std::cerr << "Run: remotepad.exe stop" << std::endl;
                return 1;
            }
            std::error_code ec;
            fs::remove(pidFile, ec);
        }
    }

    std::cout << "RemotePad v" << REMOTEPAD_VERSION << std::endl;
    std::cout << REMOTEPAD_URL << std::endl;
    std::cout << std::endl;

    // Find the first non-flag argument (skip -p <port>)
    HWND target = nullptr;
    std::string cmd;
    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "-p") { ++i; continue; }
        cmd = arg;
        break;
    }

    if (cmd.empty()) {
        target = selectWindowByCursor();
    } else if (cmd == "list") {
        const auto windows = collectSelectableWindows();
        if (windows.empty()) {
            std::cerr << "No selectable top-level windows found." << std::endl;
            return 1;
        }
        printWindowList(windows);
        return 0;
    } else if (cmd == "current") {
        target = GetForegroundWindow();
    } else {
        try {
            target = parseWindowHandle(cmd);
        } catch (...) {
            std::cerr << "Invalid window handle: " << cmd << std::endl;
            showHelp(argv[0]);
            return 1;
        }
    }

    if (!IsWindow(target)) {
        std::cerr << "No window selected." << std::endl;
        return 1;
    }

    AppConfig config = loadConfig();
    if (portOverride > 0) config.port = portOverride;

    DWORD childPid = 0;
    if (!spawnBackground(target, config.port, childPid)) {
        std::cerr << "Failed to start background instance (error " << GetLastError() << ")" << std::endl;
        return 1;
    }

    // Get local LAN IP (prefer 192.168/10/172.16-31, skip loopback and VPN)
    std::string ip = "localhost";
    {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) == 0) {
            char hostname[256]{};
            if (gethostname(hostname, sizeof(hostname)) == 0) {
                struct addrinfo hints{}, *result = nullptr;
                hints.ai_family = AF_INET;
                hints.ai_socktype = SOCK_STREAM;
                if (getaddrinfo(hostname, nullptr, &hints, &result) == 0 && result) {
                    std::string fallback;
                    for (auto* rp = result; rp; rp = rp->ai_next) {
                        auto* sin = reinterpret_cast<struct sockaddr_in*>(rp->ai_addr);
                        char buf[64]{};
                        inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf));
                        uint32_t addr = ntohl(sin->sin_addr.s_addr);
                        if (addr == 0x7F000001) continue; // loopback
                        bool isLan = (addr >> 24 == 192 && (addr >> 16 & 0xFF) == 168)
                                  || (addr >> 24 == 10)
                                  || (addr >> 24 == 172 && (addr >> 16 & 0xF0) == 16);
                        if (isLan) {
                            ip = buf;
                            break;
                        }
                        if (fallback.empty()) fallback = buf;
                    }
                    if (ip == "localhost" && !fallback.empty()) ip = fallback;
                    freeaddrinfo(result);
                }
            }
            WSACleanup();
        }
    }

    std::cout << "remotepad running (pid " << childPid << ")" << std::endl;
    std::cout << "  Tablet: http://" << ip << ":" << config.port << std::endl;
    std::cout << "  Stop:   remotepad.exe stop" << std::endl;
    return 0;
}

#endif
