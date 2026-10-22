#pragma once
#include "pch.h"
#include "Gpu.h"
#include "Tonemapper.h"

// The borderless window Discord shares. It only ever shows the tonemapped
// picture (or black), so nothing else leaks into the stream.
class OutputWindow
{
public:
    static constexpr wchar_t kTitle[] = L"Discord HDR Helper Output";

    void Create(HINSTANCE instance, const Gpu& gpu, std::function<void()> onRightClick);
    HWND Hwnd() const { return m_hwnd; }

    void Present(const Tonemapper& tonemapper);
    void SetInBackground(bool inBackground);
    // SDR white of the display the window is on, in nits (80 on an SDR display).
    void SetSdrWhite(float nits) { m_whiteScale = 80.f / std::max(nits, 1.f); }
    void SetVisible(bool visible);
    bool Visible() const { return m_hwnd && IsWindowVisible(m_hwnd); }
    // Makes the window exactly the source's resolution, even if that's larger than
    // the screen. Discord records the window at its size, so anything smaller loses detail.
    void SizeToSource(UINT width, UINT height);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);
    // Sizes the swap chain buffers. With a picture, they hold it at its native
    // resolution (padded to the window's aspect ratio) and Windows scales them
    // to the window, so whatever reads the buffers gets every source pixel.
    void Resize();
    void DrawIdle();

    HWND m_hwnd = nullptr;
    const Gpu* m_gpu = nullptr;
    std::function<void()> m_onRightClick;
    bool m_inBackground = true;

    winrt::com_ptr<IDXGISwapChain1> m_swapChain;
    winrt::com_ptr<ID3D11RenderTargetView> m_rtv;
    winrt::com_ptr<ID3D11PixelShader> m_blitPS;
    winrt::com_ptr<ID3D11Buffer> m_params;
    float m_whiteScale = 1.f;
    UINT m_width = 0, m_height = 0; // Swap chain buffer size
    UINT m_sourceWidth = 0, m_sourceHeight = 0; // 0 while idle
    UINT m_sizedWidth = 0, m_sizedHeight = 0;   // Source size the window was last fitted to

    winrt::com_ptr<ID2D1Factory1> m_d2dFactory;
    winrt::com_ptr<IDWriteFactory> m_dwrite;
    winrt::com_ptr<ID2D1RenderTarget> m_d2dTarget;
};
