#include "Window.h"
#include <windowsx.h>

static void LOGW(const wchar_t* msg) { OutputDebugStringW(msg); }

Window::Window(HINSTANCE hInst, const WindowConfig& cfg)
    : _hInst(hInst), _cfg(cfg)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    registerClass();
    createWindow();
}

Window::~Window()
{
    _cfg.save();
}

bool Window::processMessages()
{
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) return false;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return true;
}

uint32_t Window::getClientWidth() const
{
    RECT r{}; GetClientRect(_hwnd, &r);
    return static_cast<uint32_t>(r.right - r.left);
}
uint32_t Window::getClientHeight() const
{
    RECT r{}; GetClientRect(_hwnd, &r);
    return static_cast<uint32_t>(r.bottom - r.top);
}

void Window::toggleBorderless()
{
    _cfg.borderless = !_cfg.borderless;
    DWORD style = _cfg.borderless ? WS_POPUP : WS_OVERLAPPEDWINDOW;
    SetWindowLongW(_hwnd, GWL_STYLE, static_cast<LONG>(style));
    RECT r; GetWindowRect(_hwnd, &r);
    SetWindowPos(_hwnd, nullptr, r.left, r.top,
                 r.right - r.left, r.bottom - r.top,
                 SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE);
}

void Window::registerClass()
{
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = _hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"FiveMapperWindowClass";
    RegisterClassExW(&wc);
}

void Window::createWindow()
{
    DWORD style = _cfg.borderless ? WS_POPUP : WS_OVERLAPPEDWINDOW;

    _hwnd = CreateWindowExW(
        0,
        L"FiveMapperWindowClass",
        L"FiveMapper",
        style,
        static_cast<int>(_cfg.x),
        static_cast<int>(_cfg.y),
        static_cast<int>(_cfg.width),
        static_cast<int>(_cfg.height),
        nullptr, nullptr, _hInst, this
    );

    ShowWindow(_hwnd, SW_SHOW);
    UpdateWindow(_hwnd);
}

LRESULT CALLBACK Window::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    Window* self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (msg == WM_NCCREATE) {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = reinterpret_cast<Window*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg)
    {
    case WM_SETCURSOR:
        // hack for removing the spinning cursor icon
        if (LOWORD(lp) == HTCLIENT) 
        {
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
            return TRUE;
        }
        break;

    case WM_KEYDOWN:
        if (wp == VK_RETURN && (GetKeyState(VK_MENU) & 0x8000))
        {
            self->toggleBorderless();
            return 0;
        }
        break;

    case WM_SIZE:
        if (wp != SIZE_MINIMIZED)
        {
            self->_cfg.width  = LOWORD(lp);
            self->_cfg.height = HIWORD(lp);
        }
        break;

    case WM_MOVE:
        self->_cfg.x = LOWORD(lp);
        self->_cfg.y = HIWORD(lp);
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
