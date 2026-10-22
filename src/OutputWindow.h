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

    // onShowPanel runs on a right-click and when the window is activated from the
    // taskbar or Alt+Tab, since the output's taskbar button stands in for the panel's.
    void Create(HINSTANCE instance, const Gpu& gpu, std::function<void()> onShowPanel);
    HWND Hwnd() const { return m_hwnd; }

    void Present(const Tonemapper& tonemapper);
    void SetInBackground(bool inBackground);
    void SetVisible(bool visible);
    bool Visible() const { return m_hwnd && IsWindowVisible(m_hwnd); }
    // Match the source resolution where it fits on screen, so Discord gets full detail.
    void SizeToSource(UINT width, UINT height);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);
    void Resize();
    void DrawIdle();

    HWND m_hwnd = nullptr;
    const Gpu* m_gpu = nullptr;
    std::function<void()> m_onShowPanel;
    bool m_inBackground = true;

    winrt::com_ptr<IDXGISwapChain1> m_swapChain;
    winrt::com_ptr<ID3D11RenderTargetView> m_rtv;
    winrt::com_ptr<ID3D11PixelShader> m_blitPS;
    UINT m_width = 0, m_height = 0;

    winrt::com_ptr<ID2D1Factory1> m_d2dFactory;
    winrt::com_ptr<IDWriteFactory> m_dwrite;
    winrt::com_ptr<ID2D1RenderTarget> m_d2dTarget;
};
