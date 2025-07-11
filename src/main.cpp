#include <windows.h>
#include "platform/Window.h"

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int)
{
    wchar_t buf[MAX_PATH];
    GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    std::wstring cfgPath = std::wstring(buf) + L"\\FiveMapper\\config.json";

    WindowConfig cfg = WindowConfig::load(cfgPath);
    Window win(hInst, cfg);

    while (win.processMessages())
    {
        POINT md = win.getMouseDelta();
        // TODO: update camera using md.x, md.y
        // TODO: render via DX12
    }
    return 0;
}
