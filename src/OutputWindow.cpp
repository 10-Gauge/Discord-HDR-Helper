#include "OutputWindow.h"

namespace
{
    constexpr wchar_t kClass[] = L"DiscordHDRHelperOutput";

    // The tonemapped picture is sRGB-encoded SDR. whiteScale dims it in linear
    // light (the Stream brightness setting); a little dither hides banding.
    constexpr char kBlitPS[] = R"(
Texture2D<float4> image : register(t0);
SamplerState linearClamp : register(s0);
cbuffer Params : register(b0) { float whiteScale; float3 padding; };

float3 SrgbToLinear(float3 c)
{
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

float3 LinearToSrgb(float3 c)
{
    return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1.0 / 2.4) - 0.055;
}

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
    float3 c = saturate(image.SampleLevel(linearClamp, uv, 0).rgb);
    if (whiteScale < 0.999)
    {
        c = LinearToSrgb(SrgbToLinear(c) * whiteScale);
        // Interleaved gradient noise, +-0.5 of an 8-bit step.
        float noise = frac(52.9829189 * frac(dot(pos.xy, float2(0.06711056, 0.00583715))));
        c = saturate(c + (noise - 0.5) / 255.0);
    }
    return float4(c, 1.0);
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
    m_hwnd = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_NOREDIRECTIONBITMAP, kClass, kTitle, WS_POPUP, x, y, w, h,
                             nullptr, nullptr, instance, this);
    winrt::check_bool(m_hwnd != nullptr);

    // The picture goes to the window through DirectComposition, the way browsers
    // draw, not through a swap chain. Discord treats a window that presents a
    // swap chain like a game and runs its own HDR processing on it, which
    // washes the stream out or darkens it. A composition surface is captured
    // like any other window.
    winrt::check_hresult(DCompositionCreateDevice(gpu.device.as<IDXGIDevice>().get(), IID_PPV_ARGS(m_dcomp.put())));
    winrt::check_hresult(m_dcomp->CreateTargetForHwnd(m_hwnd, TRUE, m_dcompTarget.put()));
    winrt::check_hresult(m_dcomp->CreateVisual(m_visual.put()));
    winrt::check_hresult(m_dcompTarget->SetRoot(m_visual.get()));
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
    if (!m_dcomp)
        return;
    RECT rc{};
    GetClientRect(m_hwnd, &rc);
    UINT width = std::clamp<UINT>(rc.right - rc.left, 1, D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION);
    UINT height = std::clamp<UINT>(rc.bottom - rc.top, 1, D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION);
    if (m_surface && width == m_width && height == m_height)
        return;

    m_surface = nullptr;
    if (FAILED(m_dcomp->CreateSurface(width, height, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_IGNORE, m_surface.put())))
        return;
    m_visual->SetContent(m_surface.get());
    m_width = width;
    m_height = height;
}

void OutputWindow::Present(const Tonemapper& tonemapper)
{
    if (!m_dcomp || !Visible())
        return;
    m_sourceWidth = tonemapper.HasOutput() ? tonemapper.Width() : 0;
    m_sourceHeight = tonemapper.HasOutput() ? tonemapper.Height() : 0;
    if (m_sourceWidth && (m_sourceWidth != m_sizedWidth || m_sourceHeight != m_sizedHeight))
        SizeToSource(m_sourceWidth, m_sourceHeight); // The source was resized
    Resize();
    if (!m_surface)
        return;

    if (!tonemapper.HasOutput())
    {
        DrawIdle();
        m_dcomp->Commit();
        return;
    }

    // The surface may live in a shared texture at an offset, so draw only
    // inside our rectangle.
    winrt::com_ptr<ID3D11Texture2D> texture;
    POINT offset{};
    if (FAILED(m_surface->BeginDraw(nullptr, IID_PPV_ARGS(texture.put()), &offset)))
        return;
    auto* ctx = m_gpu->context.get();
    winrt::com_ptr<ID3D11RenderTargetView> rtv;
    if (SUCCEEDED(m_gpu->device->CreateRenderTargetView(texture.get(), nullptr, rtv.put())))
    {
        // Letterbox to keep the source's aspect ratio. The window is sized to the
        // source, so this is normally an exact 1:1 copy.
        const float black[4] = { 0, 0, 0, 1 };
        D3D11_RECT area{ offset.x, offset.y, LONG(offset.x + m_width), LONG(offset.y + m_height) };
        if (auto ctx1 = m_gpu->context.try_as<ID3D11DeviceContext1>())
            ctx1->ClearView(rtv.get(), black, &area, 1);
        float scale = std::min({ 1.f, float(m_width) / tonemapper.Width(), float(m_height) / tonemapper.Height() });
        float w = std::floor(tonemapper.Width() * scale), h = std::floor(tonemapper.Height() * scale);
        D3D11_VIEWPORT vp{ offset.x + std::floor((m_width - w) * 0.5f), offset.y + std::floor((m_height - h) * 0.5f), w, h, 0.f, 1.f };

        auto* target = rtv.get();
        auto* srv = tonemapper.OutputSrv();
        auto* sampler = m_gpu->linearClamp.get();
        ctx->OMSetRenderTargets(1, &target, nullptr);
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
    m_surface->EndDraw();
    m_dcomp->Commit();
}

void OutputWindow::DrawIdle()
{
    winrt::com_ptr<IDXGISurface> surface;
    POINT offset{};
    if (FAILED(m_surface->BeginDraw(nullptr, IID_PPV_ARGS(surface.put()), &offset)))
        return;

    float dpi = static_cast<float>(GetDpiForWindow(m_hwnd));
    auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                              D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), dpi, dpi);
    winrt::com_ptr<ID2D1RenderTarget> target;
    winrt::com_ptr<IDWriteTextFormat> format;
    if (SUCCEEDED(m_d2dFactory->CreateDxgiSurfaceRenderTarget(surface.get(), &props, target.put())) &&
        SUCCEEDED(m_dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_LIGHT, DWRITE_FONT_STYLE_NORMAL,
                                             DWRITE_FONT_STRETCH_NORMAL, 18.f, L"", format.put())))
    {
        format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // D2D works in DIPs; place our rectangle at the surface's offset.
        float toDip = 96.f / dpi;
        D2D1_RECT_F rect = D2D1::RectF(offset.x * toDip, offset.y * toDip, (offset.x + m_width) * toDip, (offset.y + m_height) * toDip);
        target->BeginDraw();
        target->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_ALIASED);
        target->Clear(D2D1::ColorF(0, 0, 0));
        winrt::com_ptr<ID2D1SolidColorBrush> brush;
        target->CreateSolidColorBrush(D2D1::ColorF(0x6D6F78), brush.put());
        if (brush)
        {
            constexpr wchar_t text[] = L"Waiting for a source…";
            target->DrawText(text, UINT32(std::size(text) - 1), format.get(), rect, brush.get());
        }
        target->PopAxisAlignedClip();
        target->EndDraw();
    }
    m_surface->EndDraw();
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
