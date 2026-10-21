#include "Capture.h"

using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using winrt::Windows::Foundation::Metadata::ApiInformation;

namespace
{
    constexpr auto kPixelFormat = DirectXPixelFormat::R16G16B16A16Float;
    constexpr int kBufferCount = 2;
}

Capture::Capture(Direct3D11::IDirect3DDevice const& device,
                 GraphicsCaptureItem const& item,
                 bool captureCursor,
                 Callback onFrameArrived,
                 Callback onClosed)
    : m_device(device), m_item(item), m_lastSize(item.Size())
{
    // Free-threaded so frames arrive even while the UI thread sits in a modal
    // loop (menus, window drag). The handler only signals; the UI thread renders.
    m_pool = Direct3D11CaptureFramePool::CreateFreeThreaded(m_device, kPixelFormat, kBufferCount, m_lastSize);
    m_frameToken = m_pool.FrameArrived([cb = std::move(onFrameArrived)](auto&&, auto&&) { cb(); });
    m_closedToken = m_item.Closed([cb = std::move(onClosed)](auto&&, auto&&) { cb(); });

    m_session = m_pool.CreateCaptureSession(m_item);
    SetCaptureCursor(captureCursor);

    // Windows 11 lets us hide the yellow "being captured" border.
    if (ApiInformation::IsPropertyPresent(L"Windows.Graphics.Capture.GraphicsCaptureSession", L"IsBorderRequired"))
    {
        try { m_session.IsBorderRequired(false); }
        catch (...) {}
    }

    m_session.StartCapture();
}

Capture::~Capture()
{
    if (m_pool)
        m_pool.FrameArrived(m_frameToken);
    if (m_item)
        m_item.Closed(m_closedToken);
    if (m_session)
        m_session.Close();
    if (m_pool)
        m_pool.Close();
}

void Capture::SetCaptureCursor(bool enabled)
{
    if (ApiInformation::IsPropertyPresent(L"Windows.Graphics.Capture.GraphicsCaptureSession", L"IsCursorCaptureEnabled"))
    {
        try { m_session.IsCursorCaptureEnabled(enabled); }
        catch (...) {}
    }
}

Direct3D11CaptureFrame Capture::TryGetLatestFrame()
{
    Direct3D11CaptureFrame latest{ nullptr };
    while (auto frame = m_pool.TryGetNextFrame())
        latest = frame;

    if (latest)
    {
        auto size = latest.ContentSize();
        if (size.Width != m_lastSize.Width || size.Height != m_lastSize.Height)
        {
            // Source was resized: rebuild the pool so later frames use the new size.
            m_lastSize = size;
            m_pool.Recreate(m_device, kPixelFormat, kBufferCount, m_lastSize);
        }
    }
    return latest;
}

GraphicsCaptureItem Capture::CreateItemForMonitor(HMONITOR monitor)
{
    auto interop = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    GraphicsCaptureItem item{ nullptr };
    winrt::check_hresult(interop->CreateForMonitor(monitor, winrt::guid_of<GraphicsCaptureItem>(), winrt::put_abi(item)));
    return item;
}

GraphicsCaptureItem Capture::CreateItemForWindow(HWND window)
{
    auto interop = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    GraphicsCaptureItem item{ nullptr };
    winrt::check_hresult(interop->CreateForWindow(window, winrt::guid_of<GraphicsCaptureItem>(), winrt::put_abi(item)));
    return item;
}

winrt::com_ptr<ID3D11Texture2D> Capture::GetTexture(Direct3D11CaptureFrame const& frame)
{
    auto access = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    winrt::com_ptr<ID3D11Texture2D> texture;
    winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(texture.put())));
    return texture;
}
