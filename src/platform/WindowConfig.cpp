#include "WindowConfig.h"
#include <windows.h>
#include <shlobj.h>     // SHGetKnownFolderPath
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

using nlohmann::json;
namespace fs = std::filesystem;

static std::wstring WStringFromUTF8(const std::string& s)
{
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), len);
    return w;
}
static std::string UTF8FromWString(const std::wstring& w)
{
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), len, nullptr, nullptr);
    return s;
}

std::wstring WindowConfig::configPath()
{
    PWSTR roaming = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DEFAULT, nullptr, &roaming)))
    {
        std::wstring path = roaming;
        CoTaskMemFree(roaming);
        path += L"\\FiveMapper";
        fs::create_directories(path);
        path += L"\\config.json";
        return path;
    }
    // fallback: текущая директория
    return L"config.json";
}

WindowConfig WindowConfig::load()
{
    WindowConfig cfg;
    const std::wstring path = configPath();
    if (!fs::exists(path))
        return cfg;

    std::ifstream f(UTF8FromWString(path), std::ios::binary);
    if (!f)
        return cfg;

    try {
        json j; f >> j;
        if (j.contains("window"))
        {
            auto& w = j["window"];
            if (w.contains("borderless")) cfg.borderless = w["borderless"].get<bool>();
            if (w.contains("x"))          cfg.x = w["x"].get<uint32_t>();
            if (w.contains("y"))          cfg.y = w["y"].get<uint32_t>();
            if (w.contains("width"))      cfg.width  = w["width"].get<uint32_t>();
            if (w.contains("height"))     cfg.height = w["height"].get<uint32_t>();
        }
    } catch (...) {
        // игнорируем битую конфигурацию
    }
    return cfg;
}

void WindowConfig::save() const
{
    const std::wstring path = configPath();

    json j;
    j["window"] = {
        {"borderless", borderless},
        {"x",          x},
        {"y",          y},
        {"width",      width},
        {"height",     height}
    };

    std::ofstream f(UTF8FromWString(path), std::ios::binary | std::ios::trunc);
    if (!f) return;
    f << j.dump(2);
}
