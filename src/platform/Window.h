#pragma once
#include <windows.h>
#include <cstdint>
#include "WindowConfig.h"

class Window
{
public:
    Window(HINSTANCE hInst, const WindowConfig& cfg);
    ~Window();

    bool processMessages();

    HWND getHWND() const { return _hwnd; }
    uint32_t getClientWidth()  const;
    uint32_t getClientHeight() const;

    void toggleBorderless();

private:
    void registerClass();
    void createWindow();

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

private:
    HINSTANCE   _hInst = nullptr;
    HWND        _hwnd  = nullptr;
    WindowConfig _cfg{};
};
