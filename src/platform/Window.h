#pragma once

#include <windows.h>
#include <string>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>

struct WindowConfig
{
    int x = 100;
    int y = 100;
    int width = 1280;
    int height = 720;
    bool borderless = false;

    static WindowConfig load()
    {
        WindowConfig cfg;

        wchar_t buf[MAX_PATH];
        GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
        std::wstring path = std::wstring(buf) + L"\\FiveMapper\\config.json";

        std::filesystem::path p(path);
        std::ifstream f(p.u8string());
        if (f)
        {
            nlohmann::json j = nlohmann::json::parse(f);
            auto w = j.value("window", nlohmann::json::object());
            cfg.x = w.value("x", cfg.x);
            cfg.y = w.value("y", cfg.y);
            int lw = w.value("width", cfg.width);
            int lh = w.value("height", cfg.height);
            // fallback if invalid sizes
            cfg.width = (lw > 100 ? lw : cfg.width);
            cfg.height = (lh > 100 ? lh : cfg.height);
            cfg.borderless = w.value("borderless", cfg.borderless);
        }
        return cfg;
    }

    void save() const
    {
        wchar_t buf[MAX_PATH];
        GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
        std::wstring path = std::wstring(buf) + L"\\FiveMapper\\config.json";

        nlohmann::json j;
        j["window"]["x"] = x;
        j["window"]["y"] = y;
        j["window"]["width"] = width;
        j["window"]["height"] = height;
        j["window"]["borderless"] = borderless;
        std::filesystem::path p(path);
        std::filesystem::create_directories(p.parent_path());
        std::ofstream f(p.u8string());
        f << j.dump(4);
    }
};

class Window
{
public:
    Window(HINSTANCE hInst, const WindowConfig& cfg);
    ~Window();

    bool processMessages();
    void toggleBorderless();
    void getSize(int& w, int& h) const;
    uint32_t getClientWidth() const;
    uint32_t getClientHeight() const;
    POINT getMouseDelta();

    HWND getHWND() const { return _hwnd; }

private:
    void registerClass();
    void createWindow();
    void initRawInput();
    void onResize(int w, int h);
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    HINSTANCE    _hInst;
    WindowConfig _cfg;
    HWND         _hwnd = nullptr;
    bool         _captured = false;
    POINT        _mouseDelta = {0,0};
};