#include "Window.h"
#include <vector>

Window::Window(HINSTANCE hInst, const WindowConfig& cfg) : _hInst(hInst), _cfg(cfg)
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
    MSG msg;
    _mouseDelta = {0,0};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) return false;
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

POINT Window::getMouseDelta()
{
    POINT d = _mouseDelta;
    _mouseDelta = {0,0};
    return d;
}

void Window::toggleBorderless()
{
    _cfg.borderless = !_cfg.borderless;
    DWORD style = _cfg.borderless ? WS_POPUP : WS_OVERLAPPEDWINDOW;
    SetWindowLongW(_hwnd, GWL_STYLE, style);
    RECT r;
    GetWindowRect(_hwnd, &r);
    SetWindowPos(_hwnd, nullptr, r.left, r.top,
                 r.right - r.left, r.bottom - r.top,
                 SWP_FRAMECHANGED | SWP_NOZORDER);
}

void Window::registerClass()
{
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = _hInst;
    wc.lpszClassName = L"FiveMapperWindowClass";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
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
}

void Window::initRawInput()
{
    RAWINPUTDEVICE rid[2] = {
        {0x01, 0x02, RIDEV_INPUTSINK, _hwnd},
        {0x01, 0x06, RIDEV_INPUTSINK, _hwnd}
    };
    RegisterRawInputDevices(rid, 2, sizeof(rid[0]));
}

void Window::onResize(int w, int h) {
    (void)w;
    (void)h;
    // TODO: recreate DX12 swapchain
}

LRESULT CALLBACK Window::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    Window* self = nullptr;
    if (msg == WM_NCCREATE)
    {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = reinterpret_cast<Window*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else
    {
        self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    switch (msg)
    {
    case WM_DPICHANGED:
    {
        RECT* r = reinterpret_cast<RECT*>(lp);
        SetWindowPos(hwnd, nullptr, r->left, r->top,
                     r->right - r->left,
                     r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        break;
    }
    case WM_INPUT:
    {
        UINT size;
        GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
        std::vector<BYTE> buf(size);
        GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_INPUT, buf.data(), &size, sizeof(RAWINPUTHEADER));
        auto* ri = reinterpret_cast<RAWINPUT*>(buf.data());
        if (ri->header.dwType == RIM_TYPEMOUSE) {
            self->_mouseDelta.x += ri->data.mouse.lLastX;
            self->_mouseDelta.y += ri->data.mouse.lLastY;
        }
        break;
    }
    case WM_KEYDOWN:
        if (wp == VK_RETURN && (GetKeyState(VK_MENU) & 0x8000))
        {
            self->toggleBorderless();
        }
        break;
    case WM_MOVE:
    {
        int newX = LOWORD(lp);
        int newY = HIWORD(lp);
        self->_cfg.x = newX;
        self->_cfg.y = newY;
        break;
    }
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED)
        {
            int newW = LOWORD(lp), newH = HIWORD(lp);
            self->_cfg.width  = newW;
            self->_cfg.height = newH;
            self->onResize(newW, newH);
        }
        break;
    //case WM_LBUTTONDOWN: - TODO probably for interacting with objects
        //SetCapture(hwnd);
        //self->_captured = true;
        //ShowCursor(FALSE);
        break;
    //case WM_LBUTTONUP: - TODO or not, better do a cool cursor than make it disappear
        //ReleaseCapture();
        //self->_captured = false;
        //ShowCursor(TRUE);
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    return 0;
}