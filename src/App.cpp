#include "App.h"
#include "Panel.h"

#include <DirectXPackedVector.h>

using namespace std::chrono;

App::App() = default;

App::~App()
{
    m_capture.reset();
    m_panel.reset();
    if (m_frameEvent)
        CloseHandle(m_frameEvent);
}

float App::EffectiveWhite() const
{
    return settings.sdrWhiteNits > 0 ? float(settings.sdrWhiteNits) : m_display.sdrWhiteNits;
}

float App::EffectivePeak() const
{
    return settings.peakNits > 0 ? float(settings.peakNits) : m_display.peakNits;
}

TonemapParams App::Params() const
{
    TonemapParams p;
    p.mode = settings.mode;
    p.sdrWhiteNits = EffectiveWhite();
    p.peakNits = EffectivePeak();
    return p;
}

void App::RefreshDisplayInfo()
{
    if (m_demo)
        return;
    HMONITOR monitor = m_sourceMonitor;
    if (m_sourceWindow)
        monitor = MonitorFromWindow(m_sourceWindow, MONITOR_DEFAULTTONEAREST);
    if (!monitor)
        monitor = MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);

    float oldWhite = EffectiveWhite(), oldPeak = EffectivePeak();
    m_display = QueryDisplay(monitor);
    m_lastDisplayRefresh = steady_clock::now();

    // The Windows "SDR content brightness" slider may have moved.
    if (oldWhite != EffectiveWhite() || oldPeak != EffectivePeak())
    {
        m_tonemapper.Reprocess(Params());
        m_output.Present(m_tonemapper);
    }
}

void App::ApplySettings()
{
    m_settingsDirty = true;
    if (m_capture)
        m_capture->SetCaptureCursor(settings.captureCursor);
    m_output.SetInBackground(settings.outputInBackground);
    m_panel->SetExcludedFromCapture(settings.hidePanelFromCapture);
    m_tonemapper.Reprocess(Params());
    m_output.Present(m_tonemapper);
    UpdateAudio();
}

// Audio follows the video source: the shared window's app, or every app for a display.
void App::UpdateAudio()
{
    DWORD target = 0;
    if (settings.audioMode == 0 && m_sourceWindow)
        GetWindowThreadProcessId(m_sourceWindow, &target);
    m_audio.Configure(settings.audioEnabled && m_capture != nullptr, settings.audioDevice, target);
}

void App::StartCapture(winrt::Windows::Graphics::Capture::GraphicsCaptureItem const& item)
{
    HANDLE frameEvent = m_frameEvent;
    HWND panel = m_panel->Hwnd();
    m_capture = std::make_unique<Capture>(
        m_gpu.winrtDevice, item, settings.captureCursor,
        [frameEvent] { SetEvent(frameEvent); },
        [panel] { PostMessageW(panel, Panel::WM_APP_SOURCE_CLOSED, 0, 0); });

    m_lastError.clear();
    m_framesThisSecond = 0;
    m_fps = 0.f;
    m_fpsWindowStart = steady_clock::now();
    RefreshDisplayInfo();

    auto size = item.Size();
    m_output.SetVisible(true);
    m_output.SizeToSource(UINT(size.Width), UINT(size.Height));
    UpdateAudio();
}

void App::CaptureMonitor(const DisplayInfo& display)
{
    StopCapture();
    try
    {
        m_sourceMonitor = display.monitor;
        m_sourceName = display.friendlyName.empty() ? display.deviceName : display.friendlyName;
        StartCapture(Capture::CreateItemForMonitor(display.monitor));
    }
    catch (winrt::hresult_error const& e)
    {
        StopCapture(L"Couldn't capture that display. " + std::wstring(e.message()));
    }
}

void App::CaptureWindow(HWND window, const std::wstring& name)
{
    StopCapture();
    if (!IsWindow(window))
    {
        StopCapture(L"That window has closed.");
        return;
    }
    try
    {
        m_sourceWindow = window;
        m_sourceName = name;
        StartCapture(Capture::CreateItemForWindow(window));
    }
    catch (winrt::hresult_error const& e)
    {
        StopCapture(L"Couldn't capture that window. Games in exclusive fullscreen can't be captured; "
                    L"switch them to borderless or windowed. (" + std::wstring(e.message()) + L")");
    }
}

void App::StopCapture(const std::wstring& error)
{
    m_capture.reset();
    m_demo = false;
    m_demoFrame = nullptr;
    m_sourceMonitor = nullptr;
    m_sourceWindow = nullptr;
    m_sourceName.clear();
    m_lastError = error;
    m_fps = 0.f;
    m_tonemapper.Clear();
    RefreshDisplayInfo();
    m_output.Present(m_tonemapper);
    UpdateAudio();
}

void App::OnSourceClosed()
{
    StopCapture(L"The captured window was closed.");
}

void App::ProcessFrame()
{
    if (!m_capture)
        return;
    try
    {
        auto frame = m_capture->TryGetLatestFrame();
        if (!frame)
            return;
        auto size = frame.ContentSize();
        m_tonemapper.Process(Capture::GetTexture(frame).get(), UINT(size.Width), UINT(size.Height), Params());
        frame.Close();
        m_output.Present(m_tonemapper);
        ++m_framesThisSecond;
    }
    catch (winrt::hresult_error const& e)
    {
        StopCapture(L"Capture stopped unexpectedly. " + std::wstring(e.message()));
    }
}

// Synthetic HDR scene (sunset with a 1000-nit sun) in scRGB, used for screenshots.
void App::StartDemo()
{
    using DirectX::PackedVector::HALF;
    using DirectX::PackedVector::XMConvertFloatToHalf;
    constexpr UINT W = 1920, H = 1080;
    std::vector<HALF> pixels(size_t(W) * H * 4);
    auto mixf = [](float a, float b, float t) { return a + (b - a) * t; };

    for (UINT y = 0; y < H; ++y)
    {
        for (UINT x = 0; x < W; ++x)
        {
            float u = x / float(W), v = y / float(H);
            // Sky: deep blue overhead to a warm, bright horizon (values are nits / 80).
            float t = std::pow(std::clamp(v / 0.62f, 0.f, 1.f), 1.6f);
            float r = mixf(0.25f, 4.2f, t), g = mixf(0.55f, 2.4f, t), b = mixf(1.9f, 1.1f, t);

            // Sun and its glow, well above SDR white.
            float dx = (u - 0.68f) * (float(W) / H), dy = v - 0.47f;
            float d = std::sqrt(dx * dx + dy * dy);
            float glow = 6.f * std::exp(-d * 9.f);
            r += glow * 1.0f; g += glow * 0.75f; b += glow * 0.4f;
            if (d < 0.045f) { r = 12.5f; g = 11.8f; b = 10.5f; }

            // Layered hills in the foreground.
            float hill1 = 0.70f + 0.05f * std::sin(u * 7.f + 1.3f) + 0.03f * std::sin(u * 17.f);
            float hill2 = 0.80f + 0.04f * std::sin(u * 5.f + 4.0f) + 0.02f * std::sin(u * 23.f + 2.f);
            if (v > hill1) { r = 0.55f; g = 0.30f; b = 0.42f; }
            if (v > hill2) { r = 0.10f; g = 0.07f; b = 0.12f; }

            HALF* p = &pixels[(size_t(y) * W + x) * 4];
            p[0] = XMConvertFloatToHalf(r);
            p[1] = XMConvertFloatToHalf(g);
            p[2] = XMConvertFloatToHalf(b);
            p[3] = XMConvertFloatToHalf(1.f);
        }
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = W;
    desc.Height = H;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{ pixels.data(), W * 4 * sizeof(HALF), 0 };
    winrt::check_hresult(m_gpu.device->CreateTexture2D(&desc, &data, m_demoFrame.put()));

    m_demo = true;
    m_sourceName = L"HDR test pattern";
    m_display.hdr = true;
    m_display.sdrWhiteNits = 240.f;
    m_display.peakNits = 1000.f;
    m_tonemapper.Process(m_demoFrame.get(), W, H, Params());
}

int App::Run(HINSTANCE instance, int showCmd, const std::wstring& screenshotPath)
{
    settings.Load();
    m_gpu.Init();
    m_tonemapper.Init(m_gpu);
    m_frameEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    m_panel = std::make_unique<Panel>(*this);
    m_output.Create(instance, m_gpu, [this] { m_panel->BringToFront(); });
    m_panel->Create(instance, showCmd);

    // Show the (idle) output right away so it already appears in Discord's share picker.
    m_output.SetInBackground(settings.outputInBackground);
    m_output.SetVisible(true);
    m_panel->SetExcludedFromCapture(settings.hidePanelFromCapture);
    RefreshDisplayInfo();
    m_output.Present(m_tonemapper);

    auto start = steady_clock::now();
    bool screenshotRequested = false;
    if (!screenshotPath.empty())
    {
        settings = Settings{}; // Defaults, not the user's saved settings.
        StartDemo();
    }

    for (;;)
    {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                if (m_settingsDirty && screenshotPath.empty())
                    settings.Save();
                return int(msg.wParam);
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (WaitForSingleObject(m_frameEvent, 0) == WAIT_OBJECT_0)
            ProcessFrame();
        if (m_demo)
        {
            m_tonemapper.Process(m_demoFrame.get(), m_tonemapper.Width(), m_tonemapper.Height(), Params());
            m_output.Present(m_tonemapper);
            ++m_framesThisSecond;
        }
        if (!screenshotPath.empty() && !screenshotRequested && steady_clock::now() - start >= milliseconds(2500))
        {
            m_panel->RequestScreenshot(screenshotPath);
            screenshotRequested = true;
        }

        auto now = steady_clock::now();
        if (now - m_fpsWindowStart >= seconds(1))
        {
            m_fps = m_framesThisSecond / duration<float>(now - m_fpsWindowStart).count();
            m_framesThisSecond = 0;
            m_fpsWindowStart = now;
            // Debounced so dragging a slider doesn't write the file every frame.
            if (m_settingsDirty && screenshotPath.empty())
            {
                settings.Save();
                m_settingsDirty = false;
            }
        }
        if (now - m_lastDisplayRefresh >= seconds(2))
            RefreshDisplayInfo();

        // The panel presents with vsync, which paces this loop. When it's hidden,
        // sleep until a frame or a message arrives instead of spinning.
        if (!m_panel->Render())
            MsgWaitForMultipleObjects(1, &m_frameEvent, FALSE, 100, QS_ALLINPUT);
    }
}
