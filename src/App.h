#pragma once
#include "pch.h"
#include "AudioPassthrough.h"
#include "Capture.h"
#include "Display.h"
#include "Gpu.h"
#include "OutputWindow.h"
#include "Settings.h"
#include "Tonemapper.h"

#include <chrono>
#include <memory>

class Panel;

// Owns the capture pipeline and the windows. The control panel reads its state
// and calls the actions below; everything runs on the UI thread.
class App
{
public:
    App();
    ~App();

    // screenshotPath: when set, shows a built-in HDR test pattern, saves a
    // screenshot of the control panel to that path and exits.
    int Run(HINSTANCE instance, int showCmd, const std::wstring& screenshotPath = {});

    Settings settings;

    bool Capturing() const { return m_capture != nullptr || m_demo; }
    HMONITOR SourceMonitor() const { return m_sourceMonitor; }
    HWND SourceWindow() const { return m_sourceWindow; }
    const std::wstring& SourceName() const { return m_sourceName; }
    const std::wstring& LastError() const { return m_lastError; }
    const DisplayInfo& SourceDisplay() const { return m_display; }
    float FramesPerSecond() const { return m_fps; }

    float EffectiveWhite() const;
    float EffectivePeak() const;

    const Gpu& GetGpu() const { return m_gpu; }
    const Tonemapper& GetTonemapper() const { return m_tonemapper; }
    OutputWindow& Output() { return m_output; }
    AudioPassthrough::Status AudioStatus() const { return m_audio.GetStatus(); }

    void CaptureMonitor(const DisplayInfo& display);
    void CaptureWindow(HWND window, const std::wstring& name);
    void StopCapture(const std::wstring& error = {});
    void OnSourceClosed();

    // Call after changing `settings`: saves them and applies them right away.
    void ApplySettings();
    void RefreshDisplayInfo();

private:
    void StartCapture(winrt::Windows::Graphics::Capture::GraphicsCaptureItem const& item);
    void StartDemo();
    void ProcessFrame();
    TonemapParams Params() const;
    void UpdateAudio();
    void UpdateOutputLevel();

    Gpu m_gpu;
    Tonemapper m_tonemapper;
    OutputWindow m_output;
    std::unique_ptr<Panel> m_panel;
    std::unique_ptr<Capture> m_capture;
    AudioPassthrough m_audio;

    HANDLE m_frameEvent = nullptr;
    HMONITOR m_sourceMonitor = nullptr;
    HWND m_sourceWindow = nullptr;
    std::wstring m_sourceName;
    std::wstring m_lastError;
    DisplayInfo m_display;
    bool m_settingsDirty = false;
    bool m_demo = false;
    winrt::com_ptr<ID3D11Texture2D> m_demoFrame;

    // Rolling frame-rate measurement for the status display.
    int m_framesThisSecond = 0;
    float m_fps = 0.f;
    std::chrono::steady_clock::time_point m_fpsWindowStart = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point m_lastDisplayRefresh{};
};
