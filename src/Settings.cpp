#include "Settings.h"

namespace
{
    constexpr wchar_t kSection[] = L"Tonemap";
    constexpr wchar_t kAudioSection[] = L"Audio";

    std::wstring SettingsPath()
    {
        PWSTR appData = nullptr;
        std::wstring path;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData)))
        {
            path = appData;
            path += L"\\DiscordHDRHelper";
            CreateDirectoryW(path.c_str(), nullptr);
            path += L"\\settings.ini";
        }
        CoTaskMemFree(appData);
        return path;
    }

    void WriteInt(const std::wstring& path, const wchar_t* key, int value)
    {
        WritePrivateProfileStringW(kSection, key, std::to_wstring(value).c_str(), path.c_str());
    }
}

void Settings::Load()
{
    auto path = SettingsPath();
    if (path.empty())
        return;
    mode = static_cast<TonemapMode>(std::clamp<int>(GetPrivateProfileIntW(kSection, L"Mode", 0, path.c_str()), 0, 2));
    sdrWhiteNits = std::clamp<int>(GetPrivateProfileIntW(kSection, L"SdrWhiteNits", 0, path.c_str()), 0, 1000);
    peakNits = std::clamp<int>(GetPrivateProfileIntW(kSection, L"PeakNits", 0, path.c_str()), 0, 10000);
    captureCursor = GetPrivateProfileIntW(kSection, L"CaptureCursor", 1, path.c_str()) != 0;
    outputInBackground = GetPrivateProfileIntW(kSection, L"OutputInBackground", 1, path.c_str()) != 0;
    hidePanelFromCapture = GetPrivateProfileIntW(kSection, L"HidePanelFromCapture", 1, path.c_str()) != 0;
    streamLevel = std::clamp<int>(GetPrivateProfileIntW(kSection, L"StreamBrightness", 100, path.c_str()), 10, 100);
    audioEnabled = GetPrivateProfileIntW(kAudioSection, L"Enabled", 1, path.c_str()) != 0;
    audioMode = std::clamp<int>(GetPrivateProfileIntW(kAudioSection, L"Mode", 0, path.c_str()), 0, 1);
    wchar_t device[512] = {};
    GetPrivateProfileStringW(kAudioSection, L"Device", L"", device, ARRAYSIZE(device), path.c_str());
    audioDevice = device;
}

void Settings::Save() const
{
    auto path = SettingsPath();
    if (path.empty())
        return;
    WriteInt(path, L"Mode", static_cast<int>(mode));
    WriteInt(path, L"SdrWhiteNits", sdrWhiteNits);
    WriteInt(path, L"PeakNits", peakNits);
    WriteInt(path, L"CaptureCursor", captureCursor ? 1 : 0);
    WriteInt(path, L"OutputInBackground", outputInBackground ? 1 : 0);
    WriteInt(path, L"HidePanelFromCapture", hidePanelFromCapture ? 1 : 0);
    WriteInt(path, L"StreamBrightness", streamLevel);
    WritePrivateProfileStringW(kAudioSection, L"Enabled", audioEnabled ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(kAudioSection, L"Mode", std::to_wstring(audioMode).c_str(), path.c_str());
    WritePrivateProfileStringW(kAudioSection, L"Device", audioDevice.c_str(), path.c_str());
}
