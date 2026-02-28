#include <windows.h>

#include <cstdint>

#include "D3DApp.h"

namespace {

constexpr wchar_t kClassName[]  = L"HelloTriangleWnd";
constexpr wchar_t kWindowTitle[] = L"Phase 1-6: UV Animation";
constexpr int     kInitWidth    = 1280;
constexpr int     kInitHeight   = 720;

D3DApp* gApp = nullptr;

// High-resolution frame timer.
struct Timer {
    Timer() {
        ::QueryPerformanceFrequency(&mFreq);
        ::QueryPerformanceCounter(&mPrev);
    }

    // Returns elapsed seconds since last call.
    [[nodiscard]] float Tick() {
        LARGE_INTEGER now;
        ::QueryPerformanceCounter(&now);
        const float dt = static_cast<float>(now.QuadPart - mPrev.QuadPart)
                       / static_cast<float>(mFreq.QuadPart);
        mPrev = now;
        return dt;
    }

    LARGE_INTEGER mFreq = {};
    LARGE_INTEGER mPrev = {};
};

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SIZE:
        if (gApp && wParam != SIZE_MINIMIZED) {
            gApp->OnResize(LOWORD(lParam), HIWORD(lParam));
        }
        return 0;

    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            ::PostQuitMessage(0);
        }
        return 0;
    }
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(
    _In_     HINSTANCE hInstance,
    _In_opt_ HINSTANCE /*hPrevInstance*/,
    _In_     LPWSTR    /*lpCmdLine*/,
    _In_     int       nShowCmd)
{
    // --- Register window class ---
    WNDCLASSEXW wc    = {};
    wc.cbSize         = sizeof(wc);
    wc.style          = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc    = WndProc;
    wc.hInstance      = hInstance;
    wc.hCursor        = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground  = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName  = kClassName;

    if (!::RegisterClassExW(&wc)) return -1;

    // --- Compute centered window rect ---
    const int screenW = ::GetSystemMetrics(SM_CXSCREEN);
    const int screenH = ::GetSystemMetrics(SM_CYSCREEN);

    RECT rect = { 0, 0, kInitWidth, kInitHeight };
    ::AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    const int wndW = rect.right  - rect.left;
    const int wndH = rect.bottom - rect.top;
    const int wndX = (screenW - wndW) / 2;
    const int wndY = (screenH - wndH) / 2;

    // --- Create window ---
    HWND hwnd = ::CreateWindowExW(
        0,
        kClassName,
        kWindowTitle,
        WS_OVERLAPPEDWINDOW,
        wndX, wndY, wndW, wndH,
        nullptr, nullptr, hInstance, nullptr
    );
    if (!hwnd) return -1;

    // --- Init D3D11 ---
    D3DApp app;
    gApp = &app;

    if (!app.Init(hwnd, kInitWidth, kInitHeight)) {
        ::MessageBoxW(nullptr, L"Failed to initialize Direct3D 11.", kWindowTitle, MB_ICONERROR);
        return -1;
    }

    ::ShowWindow(hwnd, nShowCmd);
    ::UpdateWindow(hwnd);

    // --- Message loop ---
    Timer timer;
    MSG   msg = {};

    while (msg.message != WM_QUIT) {
        if (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        } else {
            const float dt = timer.Tick();
            app.Update(dt);
            app.Render();
        }
    }

    gApp = nullptr;
    return static_cast<int>(msg.wParam);
}
