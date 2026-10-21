#pragma once
#include "pch.h"
#include "AudioPassthrough.h"
#include "Display.h"

#include <chrono>

class App;
struct ImFont;

// The control panel: a Dear ImGui window with a live preview of the
// tonemapped output, the source picker and the picture settings.
class Panel
{
public:
    static constexpr UINT WM_APP_SOURCE_CLOSED = WM_APP + 1;

    explicit Panel(App& app);
    ~Panel();

    void Create(HINSTANCE instance, int showCmd);
    HWND Hwnd() const { return m_hwnd; }

    // Draws and presents one frame (with vsync). Returns false if the panel is hidden or minimized.
    bool Render();
    void BringToFront();
    void SetExcludedFromCapture(bool excluded);
    // Saves the next rendered frame as a PNG, then quits the app.
    void RequestScreenshot(const std::wstring& path) { m_screenshotPath = path; }

private:
    struct WindowEntry
    {
        HWND hwnd = nullptr;
        std::wstring title;
        std::string titleUtf8;
        std::string processUtf8;
    };

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);
    void ResizeSwapChain();
    void BuildFonts();
    void ApplyStyle();
    void RefreshSources(bool force);

    void DrawHeader();
    void DrawPreview(float width, float height);
    void DrawSources(float width, float height);
    void DrawPicture(float width);
    void DrawAudio(float width);
    void DrawOptions(float width);
    void DrawHowTo(float width, float height);

    App& m_app;
    HWND m_hwnd = nullptr;
    winrt::com_ptr<IDXGISwapChain1> m_swapChain;
    winrt::com_ptr<ID3D11RenderTargetView> m_rtv;
    bool m_imguiReady = false;

    float m_scale = 1.f;
    bool m_rebuildFonts = true;
    ImFont* m_fontBody = nullptr;
    ImFont* m_fontSemibold = nullptr;
    ImFont* m_fontTitle = nullptr;
    ImFont* m_fontSmall = nullptr;
    ImFont* m_fontIcon = nullptr;
    ImFont* m_fontHeroIcon = nullptr;

    int m_sourceTab = 0;   // 0 = displays, 1 = windows
    int m_settingsTab = 0; // 0 = picture, 1 = audio, 2 = options
    std::vector<AudioPassthrough::Device> m_audioDevices;
    std::vector<DisplayInfo> m_displays;
    std::vector<WindowEntry> m_windows;
    std::chrono::steady_clock::time_point m_lastSourceRefresh{};
    std::wstring m_screenshotPath;
};
