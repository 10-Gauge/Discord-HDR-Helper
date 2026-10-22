#include "OutputWindow.h"

namespace
{
    constexpr wchar_t kClass[] = L"DiscordHDRHelperOutput";

    constexpr char kBlitPS[] = R"(
Texture2D<float4> image : register(t0);
SamplerState linearClamp : register(s0);

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
    return float4(image.SampleLevel(linearClamp, uv, 0).rgb, 1.0);
}
)";
}

void OutputWindow::Create(HINSTANCE instance, const Gpu& gpu, std::function<void()> onShowPanel)
{
    m_gpu = &gpu;
    m_onShowPanel = std::move(onShowPanel);

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

    m_swapChain = gpu.CreateSwapChain(m_hwnd);
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
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &mi);
    double maxW = mi.rcWork.right - mi.rcWork.left, maxH = mi.rcWork.bottom - mi.rcWork.top;
    double scale = std::min({ 1.0, maxW / width, maxH / height });
    int w = std::max(1, int(width * scale)), h = std::max(1, int(height * scale));

    RECT rc{};
    GetWindowRect(m_hwnd, &rc);
    int x = std::clamp<int>(rc.left, mi.rcWork.left, std::max<int>(mi.rcWork.left, mi.rcWork.right - w));
    int y = std::clamp<int>(rc.top, mi.rcWork.top, std::max<int>(mi.rcWork.top, mi.rcWork.bottom - h));
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
    auto* ctx = m_gpu->context.get();
    const float black[4] = { 0, 0, 0, 1 };
    ctx->ClearRenderTargetView(m_rtv.get(), black);

    if (tonemapper.HasOutput())
    {
        // Letterbox to keep the source's aspect ratio.
        float scale = std::min(float(m_width) / tonemapper.Width(), float(m_height) / tonemapper.Height());
        float w = tonemapper.Width() * scale, h = tonemapper.Height() * scale;
        D3D11_VIEWPORT vp{ (m_width - w) * 0.5f, (m_height - h) * 0.5f, w, h, 0.f, 1.f };

        auto* rtv = m_rtv.get();
        auto* srv = tonemapper.OutputSrv();
        auto* sampler = m_gpu->linearClamp.get();
        ctx->OMSetRenderTargets(1, &rtv, nullptr);
        ctx->RSSetViewports(1, &vp);
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(m_gpu->fullscreenVS.get(), nullptr, 0);
        ctx->PSSetShader(m_blitPS.get(), nullptr, 0);
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
                                                  D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), dpi, dpi);
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
        // Borderless: drag anywhere to move, edges to resize.
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(m_hwnd, &pt);
        RECT rc{};
        GetClientRect(m_hwnd, &rc);
        const int b = MulDiv(8, GetDpiForWindow(m_hwnd), 96);
        bool l = pt.x < b, r = pt.x >= rc.right - b, t = pt.y < b, btm = pt.y >= rc.bottom - b;
        if (t && l) return HTTOPLEFT;
        if (t && r) return HTTOPRIGHT;
        if (btm && l) return HTBOTTOMLEFT;
        if (btm && r) return HTBOTTOMRIGHT;
        if (l) return HTLEFT;
        if (r) return HTRIGHT;
        if (t) return HTTOP;
        if (btm) return HTBOTTOM;
        return HTCAPTION;
    }
    case WM_NCRBUTTONDOWN:
        return 0;
    case WM_NCRBUTTONUP:
    case WM_CONTEXTMENU:
        if (m_onShowPanel)
            m_onShowPanel();
        return 0;
    case WM_ACTIVATE:
    {
        // Activated from the taskbar or Alt+Tab, not by a click to drag it, and not
        // because our own panel just closed or hid.
        DWORD pid = 0;
        if (HWND previous = reinterpret_cast<HWND>(lp))
            GetWindowThreadProcessId(previous, &pid);
        if (LOWORD(wp) == WA_ACTIVE && pid != GetCurrentProcessId() && m_onShowPanel)
            m_onShowPanel();
        break;
    }
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
