#include "OutputWindow.h"

namespace
{
    constexpr wchar_t kClass[] = L"DiscordHDRHelperOutput";

    // The tonemapped picture is sRGB-encoded SDR. The output swap chain is
    // linear scRGB, where 1.0 = 80 nits, so the picture is decoded and scaled
    // so that its white lands at 80 nits. Windows brightens normal SDR windows
    // to the "SDR content brightness" level on an HDR display, and Discord
    // records that brightened version and clips it, which washes the stream
    // out. At 80 nits nothing is brightened, so Discord gets the picture as is.
    constexpr char kBlitPS[] = R"(
Texture2D<float4> image : register(t0);
SamplerState linearClamp : register(s0);
cbuffer Params : register(b0) { float whiteScale; float3 padding; };

float3 SrgbToLinear(float3 c)
{
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
    float3 c = saturate(image.SampleLevel(linearClamp, uv, 0).rgb);
    return float4(SrgbToLinear(c) * whiteScale, 1.0);
}
)";
}

void OutputWindow::Create(HINSTANCE instance, const Gpu& gpu, std::function<void()> onRightClick)
{
    m_gpu = &gpu;
    m_onRightClick = std::move(onRightClick);

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_SIZEALL);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);

    HMONITOR primary = MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(primary, &mi);
    int w = 1280, h = 720;
    int x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - w) / 2;
    int y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - h) / 2;

    // No minimize box on purpose: Discord can't capture a minimized window.
    m_hwnd = CreateWindowExW(WS_EX_APPWINDOW, kClass, kTitle, WS_POPUP, x, y, w, h,
                             nullptr, nullptr, instance, this);
    winrt::check_bool(m_hwnd != nullptr);

    m_swapChain = gpu.CreateSwapChain(m_hwnd, DXGI_FORMAT_R16G16B16A16_FLOAT);
    if (auto swapChain3 = m_swapChain.try_as<IDXGISwapChain3>())
        swapChain3->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709);
    D3D11_BUFFER_DESC cb{ 16, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
    winrt::check_hresult(gpu.device->CreateBuffer(&cb, nullptr, m_params.put()));
    auto ps = CompileShader(kBlitPS, sizeof(kBlitPS) - 1, "main", "ps_5_0");
    winrt::check_hresult(gpu.device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, m_blitPS.put()));

    winrt::check_hresult(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_d2dFactory.put()));
    winrt::check_hresult(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                             reinterpret_cast<::IUnknown**>(m_dwrite.put())));
    Resize();
}

void OutputWindow::SetVisible(bool visible)
{
    if (visible == Visible())
        return;
    ShowWindow(m_hwnd, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
    if (visible && m_inBackground)
        SetWindowPos(m_hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void OutputWindow::SetInBackground(bool inBackground)
{
    m_inBackground = inBackground;
    SetWindowPos(m_hwnd, inBackground ? HWND_BOTTOM : HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void OutputWindow::SizeToSource(UINT width, UINT height)
{
    if (width == 0 || height == 0)
        return;
    m_sizedWidth = width;
    m_sizedHeight = height;
    int w = int(width), h = int(height);

    // Keep the window on its monitor where it fits; anchor it to the monitor's
    // top-left corner where it doesn't.
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &mi);
    const RECT& area = mi.rcMonitor;
    RECT rc{};
    GetWindowRect(m_hwnd, &rc);
    int x = std::clamp<int>(rc.left, area.left, std::max<int>(area.left, area.right - w));
    int y = std::clamp<int>(rc.top, area.top, std::max<int>(area.top, area.bottom - h));
    SetWindowPos(m_hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

void OutputWindow::Resize()
{
    if (!m_swapChain)
        return;
    RECT rc{};
    GetClientRect(m_hwnd, &rc);
    UINT width = std::max<UINT>(rc.right - rc.left, 1);
    UINT height = std::max<UINT>(rc.bottom - rc.top, 1);
    if (m_sourceWidth && m_sourceHeight)
    {
        // Smallest buffer with the window's aspect ratio that holds the source 1:1.
        double aspect = double(width) / height;
        width = std::max<UINT>(m_sourceWidth, UINT(std::lround(m_sourceHeight * aspect)));
        height = std::max<UINT>(m_sourceHeight, UINT(std::lround(m_sourceWidth / aspect)));
        width = std::min<UINT>(width, D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION);
        height = std::min<UINT>(height, D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION);
    }
    if (m_rtv && width == m_width && height == m_height)
        return;

    m_gpu->context->OMSetRenderTargets(0, nullptr, nullptr);
    m_rtv = nullptr;
    m_d2dTarget = nullptr;
    m_gpu->context->Flush();
    winrt::check_hresult(m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0));
    m_width = width;
    m_height = height;

    winrt::com_ptr<ID3D11Texture2D> backBuffer;
    winrt::check_hresult(m_swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.put())));
    winrt::check_hresult(m_gpu->device->CreateRenderTargetView(backBuffer.get(), nullptr, m_rtv.put()));
}

void OutputWindow::Present(const Tonemapper& tonemapper)
{
    if (!m_rtv || !Visible())
        return;
    m_sourceWidth = tonemapper.HasOutput() ? tonemapper.Width() : 0;
    m_sourceHeight = tonemapper.HasOutput() ? tonemapper.Height() : 0;
    if (m_sourceWidth && (m_sourceWidth != m_sizedWidth || m_sourceHeight != m_sizedHeight))
        SizeToSource(m_sourceWidth, m_sourceHeight); // The source was resized
    Resize();
    auto* ctx = m_gpu->context.get();
    const float black[4] = { 0, 0, 0, 1 };
    ctx->ClearRenderTargetView(m_rtv.get(), black);

    if (tonemapper.HasOutput())
    {
        // Letterbox to keep the source's aspect ratio. The buffer is at least the
        // source's size, so this is normally an exact 1:1 copy on whole pixels.
        float scale = std::min({ 1.f, float(m_width) / tonemapper.Width(), float(m_height) / tonemapper.Height() });
        float w = std::floor(tonemapper.Width() * scale), h = std::floor(tonemapper.Height() * scale);
        D3D11_VIEWPORT vp{ std::floor((m_width - w) * 0.5f), std::floor((m_height - h) * 0.5f), w, h, 0.f, 1.f };

        auto* rtv = m_rtv.get();
        auto* srv = tonemapper.OutputSrv();
        auto* sampler = m_gpu->linearClamp.get();
        ctx->OMSetRenderTargets(1, &rtv, nullptr);
        ctx->RSSetViewports(1, &vp);
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(m_gpu->fullscreenVS.get(), nullptr, 0);
        ctx->PSSetShader(m_blitPS.get(), nullptr, 0);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(ctx->Map(m_params.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            const float params[4] = { m_whiteScale, 0.f, 0.f, 0.f };
            memcpy(mapped.pData, params, sizeof(params));
            ctx->Unmap(m_params.get(), 0);
        }
        auto* params = m_params.get();
        ctx->PSSetConstantBuffers(0, 1, &params);
        ctx->PSSetShaderResources(0, 1, &srv);
        ctx->PSSetSamplers(0, 1, &sampler);
        ctx->Draw(3, 0);
        ID3D11ShaderResourceView* nullSrv = nullptr;
        ctx->PSSetShaderResources(0, 1, &nullSrv);
        ctx->OMSetRenderTargets(0, nullptr, nullptr);
    }
    else
    {
        DrawIdle();
    }

    // Don't wait for vsync here; the control panel's present paces the loop.
    m_swapChain->Present(0, 0);
}

void OutputWindow::DrawIdle()
{
    if (!m_d2dTarget)
    {
        winrt::com_ptr<IDXGISurface> surface;
        if (FAILED(m_swapChain->GetBuffer(0, IID_PPV_ARGS(surface.put()))))
            return;
        float dpi = static_cast<float>(GetDpiForWindow(m_hwnd));
        auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                                  D2D1::PixelFormat(DXGI_FORMAT_R16G16B16A16_FLOAT, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi, dpi);
        if (FAILED(m_d2dFactory->CreateDxgiSurfaceRenderTarget(surface.get(), &props, m_d2dTarget.put())))
            return;
    }

    winrt::com_ptr<IDWriteTextFormat> format;
    if (FAILED(m_dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_LIGHT, DWRITE_FONT_STYLE_NORMAL,
                                          DWRITE_FONT_STRETCH_NORMAL, 18.f, L"", format.put())))
        return;
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    m_d2dTarget->BeginDraw();
    winrt::com_ptr<ID2D1SolidColorBrush> brush;
    m_d2dTarget->CreateSolidColorBrush(D2D1::ColorF(0x6D6F78), brush.put());
    if (brush)
    {
        constexpr wchar_t text[] = L"Waiting for a source…";
        auto size = m_d2dTarget->GetSize();
        m_d2dTarget->DrawText(text, UINT32(std::size(text) - 1), format.get(), D2D1::RectF(0, 0, size.width, size.height), brush.get());
    }
    if (m_d2dTarget->EndDraw() == D2DERR_RECREATE_TARGET)
        m_d2dTarget = nullptr;
}

LRESULT CALLBACK OutputWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCCREATE)
    {
        auto* self = static_cast<OutputWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    }
    auto* self = reinterpret_cast<OutputWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->HandleMessage(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT OutputWindow::HandleMessage(UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_NCHITTEST:
    {
        // Borderless: drag anywhere to move. There's no resizing, since the
        // size always follows the source.
        return HTCAPTION;
    }
    case WM_GETMINMAXINFO:
    {
        // Allow windows larger than the screen, e.g. a 4K source on a 1440p display.
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMaxTrackSize = { 16384, 16384 };
        mmi->ptMaxSize = { 16384, 16384 };
        return 0;
    }
    case WM_NCRBUTTONDOWN:
        return 0;
    case WM_NCRBUTTONUP:
    case WM_CONTEXTMENU:
        if (m_onRightClick)
            m_onRightClick();
        return 0;
    case WM_SYSCOMMAND:
        // Discord can't capture a minimized window, so the taskbar can't minimize it either.
        if ((wp & 0xFFF0) == SC_MINIMIZE)
            return 0;
        break;
    case WM_WINDOWPOSCHANGING:
        if (m_inBackground)
        {
            // Stay under every other window. Discord still captures it while it's covered.
            auto* pos = reinterpret_cast<WINDOWPOS*>(lp);
            pos->hwndInsertAfter = HWND_BOTTOM;
            pos->flags &= ~SWP_NOZORDER;
        }
        break;
    case WM_SIZE:
        Resize();
        return 0;
    case WM_PAINT:
        ValidateRect(m_hwnd, nullptr);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_CLOSE:
        // Closing is done from the control panel.
        return 0;
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}
