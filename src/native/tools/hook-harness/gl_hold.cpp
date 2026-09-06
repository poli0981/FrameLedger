// hook-harness --opengl --hold-presenting N: a REAL OpenGL context on a hidden
// window, SwapBuffers'd every few milliseconds for N seconds, so the Overlay's
// wglSwapBuffers hook can be exercised with no game (17_HOOK_ENGINE §Test
// harness; P1 item 4). Microsoft's GDI Generic renderer answers on any Windows
// install, GPU or not, so this mode runs on CI as well.
//
// The application calls gdi32's SwapBuffers -- as SDL, GLFW and every engine do --
// and NOT opengl32!wglSwapBuffers directly, so what is measured is whether the
// route a real title takes reaches the hook. Exit codes:
//   0   presented for the whole hold
//   77  this machine cannot run the fixture (no window, no pixel format, no context)
//   2   the fixture is broken

#include <windows.h>

#include <gl/GL.h>

#include <cstdio>

namespace {

LRESULT CALLBACK HiddenGlWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    return DefWindowProcW(h, m, w, l);
}

int Skip(const char* why) {
    std::printf("  [SKIP] %s\n", why);
    std::fflush(stdout);
    return 77;
}

}    // namespace

int HoldPresentingOpenGl(int seconds, int presentIntervalMs) {
    std::printf("\n[opengl-hold] a real OpenGL context on a hidden window, presenting for %d second(s) every %d ms\n",
                seconds, presentIntervalMs);
    std::fflush(stdout);

    WNDCLASSW wc{};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = &HiddenGlWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"FrameLedgerHookHarnessOpenGL";
    RegisterClassW(&wc);
    // Never shown: a pixel format needs an HDC of a real window, not a visible one.
    const HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"FrameLedger hook-harness (OpenGL)", WS_OVERLAPPEDWINDOW, 0,
                                      0, 320, 200, nullptr, nullptr, wc.hInstance, nullptr);
    if (hwnd == nullptr) {
        return Skip("no window could be created (no interactive window station?)");
    }
    const HDC hdc = GetDC(hwnd);
    if (hdc == nullptr) {
        return Skip("no device context for the window");
    }

    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;
    const int format = ChoosePixelFormat(hdc, &pfd);
    if (format == 0 || !SetPixelFormat(hdc, format, &pfd)) {
        return Skip("no double-buffered OpenGL pixel format on this device");
    }
    const HGLRC ctx = wglCreateContext(hdc);
    if (ctx == nullptr || !wglMakeCurrent(hdc, ctx)) {
        return Skip("wglCreateContext / wglMakeCurrent failed");
    }
    const auto* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    std::printf("  renderer: %s\n", renderer != nullptr ? renderer : "(unknown)");
    std::fflush(stdout);

    const ULONGLONG until = GetTickCount64() + static_cast<ULONGLONG>(seconds) * 1000ULL;
    long long       presented = 0;
    while (GetTickCount64() < until) {
        glClearColor(0.1f, 0.2f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        if (!SwapBuffers(hdc)) {    // gdi32, the route a real title takes
            std::printf("  [FAIL] SwapBuffers failed (%lu)\n", GetLastError());
            return 2;
        }
        ++presented;
        if (presentIntervalMs > 0) {
            Sleep(static_cast<DWORD>(presentIntervalMs));
        }
    }
    // The count goes to stdout so a test can compare it against records drained
    // from the ring rather than asserting "more than zero".
    std::printf("  presented=%lld\n", presented);
    std::fflush(stdout);
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(ctx);
    ReleaseDC(hwnd, hdc);
    DestroyWindow(hwnd);
    return 0;
}
