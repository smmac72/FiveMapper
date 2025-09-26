#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
#include "WindowConfig.h"

class Window
{
public:
    explicit Window(HINSTANCE hInst, const WindowConfig& cfg);
    ~Window();

    bool processMessages();

    void getSize(int& w, int& h) const;
    uint32_t getClientWidth() const;
    uint32_t getClientHeight() const;

    HWND getHWND() const
    {
        return _hwnd;
    }

    // returns and resets per–frame raw mouse delta
    POINT getMouseDelta()
    {
        POINT d = _mouseDelta;
        _mouseDelta.x = 0;
        _mouseDelta.y = 0;
        return d;
    }

    // hybrid key state: our cached state OR winapi async bit
    // this makes input robust even if some messages are missed
    bool isKeyDown(int vk) const
    {
        if (vk < 0 || vk > 255) return false;
        // note: GetAsyncKeyState returns short with high bit = down
        return _keyDown[vk] || ((GetAsyncKeyState(vk) & 0x8000) != 0);
    }

    // optional alias
    bool keyDown(int vk) const
    {
        return isKeyDown(vk);
    }

    void toggleBorderless();

private:
    void registerClass();
    void createWindow();
    void initRawInput();
    void onResize(int w, int h);

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

private:
    HINSTANCE    _hInst = nullptr;
    HWND         _hwnd  = nullptr;
    WindowConfig _cfg{};

    // raw input accumulation for this frame
    POINT _mouseDelta { 0, 0 };

    // true while lmb is held and mouse is captured
    bool _captured = false;

    // keyboard state (per virtual-key code)
    bool _keyDown[256] = {};
};
