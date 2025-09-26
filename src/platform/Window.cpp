#include "Window.h"
#include <vector>
#include <cstring>

static void LOGW(const wchar_t* s)
{
    OutputDebugStringW(s);
    OutputDebugStringW(L"\n");
}

Window::Window(HINSTANCE hInst, const WindowConfig& cfg)
    : _hInst(hInst), _cfg(cfg)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    registerClass();
    createWindow();
    initRawInput();
}

Window::~Window()
{
    _cfg.save();
}

bool Window::processMessages()
{
    _mouseDelta = {0, 0};

    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        if (msg.message == WM_QUIT)
        {
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return true;
}

void Window::getSize(int& w, int& h) const
{
    RECT r;
    GetClientRect(_hwnd, &r);
    w = r.right - r.left;
    h = r.bottom - r.top;
}

uint32_t Window::getClientWidth() const
{
    RECT r;
    GetClientRect(_hwnd, &r);
    return r.right - r.left;
}

uint32_t Window::getClientHeight() const
{
    RECT r;
    GetClientRect(_hwnd, &r);
    return r.bottom - r.top;
}

void Window::toggleBorderless()
{
    _cfg.borderless = !_cfg.borderless;
    DWORD style = _cfg.borderless ? WS_POPUP : WS_OVERLAPPEDWINDOW;
    SetWindowLongW(_hwnd, GWL_STYLE, style);
    RECT r;
    GetWindowRect(_hwnd, &r);
    SetWindowPos(
        _hwnd, nullptr,
        r.left, r.top,
        r.right - r.left, r.bottom - r.top,
        SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE
    );
}

void Window::registerClass()
{
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = _hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
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
        _cfg.x, _cfg.y,
        _cfg.width, _cfg.height,
        nullptr, nullptr, _hInst, this
    );

    ShowWindow(_hwnd, SW_SHOW);
    UpdateWindow(_hwnd);
    SetForegroundWindow(_hwnd);
    SetFocus(_hwnd);
}

void Window::initRawInput()
{
    RAWINPUTDEVICE rid[2] = {};

    rid[0].usUsagePage = 0x01; // mouse
    rid[0].usUsage     = 0x02;
    rid[0].dwFlags     = RIDEV_INPUTSINK;
    rid[0].hwndTarget  = _hwnd;

    rid[1].usUsagePage = 0x01; // keyboard
    rid[1].usUsage     = 0x06;
    rid[1].dwFlags     = RIDEV_INPUTSINK;
    rid[1].hwndTarget  = _hwnd;

    if (!RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE)))
    {
        DWORD err = GetLastError();
        wchar_t buf[256];
        swprintf(buf, 256, L"[wnd] RegisterRawInputDevices failed. gle=%lu", err);
        LOGW(buf);
    }
    else
    {
        LOGW(L"[wnd] raw input registered ok");
    }
}

void Window::onResize(int w, int h)
{
    (void)w;
    (void)h;
    // todo: recreate dx12 swapchain and gbuffer
}

LRESULT CALLBACK Window::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    Window* self = nullptr;

    if (msg == WM_NCCREATE)
    {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = reinterpret_cast<Window*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else
    {
        self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (!self)
    {
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    switch (msg)
    {
    case WM_ACTIVATE:
    {
        if (LOWORD(wp) == WA_INACTIVE)
        {
            std::memset(self->_keyDown, 0, sizeof(self->_keyDown));
        }
        break;
    }

    case WM_KILLFOCUS:
    {
        std::memset(self->_keyDown, 0, sizeof(self->_keyDown));
        break;
    }

    case WM_INPUT:
    {
        UINT size = 0;
        GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
        if (size == 0) break;

        std::vector<BYTE> buf(size);
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_INPUT, buf.data(), &size, sizeof(RAWINPUTHEADER)) != size)
        {
            break;
        }

        RAWINPUT* ri = reinterpret_cast<RAWINPUT*>(buf.data());

        if (ri->header.dwType == RIM_TYPEMOUSE)
        {
            self->_mouseDelta.x += ri->data.mouse.lLastX;
            self->_mouseDelta.y += ri->data.mouse.lLastY;
        }
        else if (ri->header.dwType == RIM_TYPEKEYBOARD)
        {
            const RAWKEYBOARD& rk = ri->data.keyboard;

            if (rk.VKey == 255) break;

            USHORT vk = rk.VKey;
            if (vk == VK_SHIFT)
            {
                vk = static_cast<USHORT>(MapVirtualKey(rk.MakeCode, MAPVK_VSC_TO_VK_EX));
            }

            bool isBreak = (rk.Flags & RI_KEY_BREAK) != 0;
            bool isMake  = !isBreak;

            if (vk <= 255)
            {
                self->_keyDown[vk] = isMake;
            }
        }
        break;
    }

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    {
        int vk = static_cast<int>(wp & 0xFF);
        if (vk >= 0 && vk <= 255)
        {
            self->_keyDown[vk] = true;
        }
        if (vk == VK_RETURN && (GetKeyState(VK_MENU) & 0x8000))
        {
            self->toggleBorderless();
        }
        break;
    }

    case WM_KEYUP:
    case WM_SYSKEYUP:
    {
        int vk = static_cast<int>(wp & 0xFF);
        if (vk >= 0 && vk <= 255)
        {
            self->_keyDown[vk] = false;
        }
        break;
    }

    case WM_MOVE:
    {
        int newX = LOWORD(lp);
        int newY = HIWORD(lp);
        self->_cfg.x = newX;
        self->_cfg.y = newY;
        break;
    }

    case WM_SIZE:
    {
        if (wp != SIZE_MINIMIZED)
        {
            int newW = LOWORD(lp);
            int newH = HIWORD(lp);
            self->_cfg.width  = newW;
            self->_cfg.height = newH;
            self->onResize(newW, newH);
        }
        break;
    }

    case WM_LBUTTONDOWN:
    {
        SetCapture(hwnd);
        self->_captured = true;
        ShowCursor(FALSE);
        // ensure keyboard focus during capture
        SetForegroundWindow(hwnd);
        SetFocus(hwnd);
        break;
    }

    case WM_LBUTTONUP:
    {
        ReleaseCapture();
        self->_captured = false;
        ShowCursor(TRUE);
        break;
    }

    case WM_DESTROY:
    {
        PostQuitMessage(0);
        break;
    }

    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    return 0;
}
