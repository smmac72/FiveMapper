#pragma once
#include <string>
#include <cstdint>

struct WindowConfig
{
    bool     borderless = false;
    uint32_t x = 100;
    uint32_t y = 100;
    uint32_t width  = 1600;
    uint32_t height = 900;

    // config path
    static std::wstring configPath();

    // load/save config
    static WindowConfig load();
    void save() const;
};
