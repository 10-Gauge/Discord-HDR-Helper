#pragma once
#include "pch.h"

// Wraps a Windows.Graphics.Capture session that delivers frames as FP16 scRGB
// (linear, BT.709 primaries, 1.0 = 80 nits), which keeps HDR highlights intact.
class Capture
{
public:
    using Callback = std::function<void()>;

    Capture(winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice const& device,
            winrt::Windows::Graphics::Capture::GraphicsCaptureItem const& item,
            bool captureCursor,
            Callback onFrameArrived,
            Callback onClosed);
    ~Capture();

    Capture(const Capture&) = delete;
    Capture& operator=(const Capture&) = delete;

    // Returns the newest pending frame (dropping older ones), or nullptr.
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame TryGetLatestFrame();

    void SetCaptureCursor(bool enabled);
    winrt::Windows::Graphics::SizeInt32 Size() const { return m_lastSize; }

    static winrt::Windows::Graphics::Capture::GraphicsCaptureItem CreateItemForMonitor(HMONITOR monitor);
    static winrt::Windows::Graphics::Capture::GraphicsCaptureItem CreateItemForWindow(HWND window);
    static winrt::com_ptr<ID3D11Texture2D> GetTexture(winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame const& frame);

private:
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice m_device{ nullptr };
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem m_item{ nullptr };
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool m_pool{ nullptr };
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession m_session{ nullptr };
    winrt::Windows::Graphics::SizeInt32 m_lastSize{};
    winrt::event_token m_frameToken{};
    winrt::event_token m_closedToken{};
};
