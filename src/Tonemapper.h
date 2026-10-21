#pragma once
#include "pch.h"
#include "Gpu.h"
#include "Settings.h"

struct TonemapParams
{
    TonemapMode mode = TonemapMode::BT2390;
    float sdrWhiteNits = 80.f; // Brightness that maps to SDR 100% white
    float peakNits = 1000.f;   // Brightest value the source can contain
};

// Turns captured FP16 scRGB frames into an 8-bit sRGB image at the source's
// resolution. Both the output window and the UI preview draw from that image.
class Tonemapper
{
public:
    void Init(const Gpu& gpu);

    void Process(ID3D11Texture2D* frame, UINT contentWidth, UINT contentHeight, const TonemapParams& params);
    // Re-run the tonemap on the last frame, e.g. after a settings change.
    void Reprocess(const TonemapParams& params);
    void Clear();

    bool HasOutput() const { return m_outputSrv != nullptr; }
    ID3D11ShaderResourceView* OutputSrv() const { return m_outputSrv.get(); }
    UINT Width() const { return m_width; }
    UINT Height() const { return m_height; }

private:
    void EnsureTextures(DXGI_FORMAT sourceFormat, UINT width, UINT height);

    const Gpu* m_gpu = nullptr;
    winrt::com_ptr<ID3D11PixelShader> m_ps;
    winrt::com_ptr<ID3D11Buffer> m_constants;

    winrt::com_ptr<ID3D11Texture2D> m_source;
    winrt::com_ptr<ID3D11ShaderResourceView> m_sourceSrv;
    winrt::com_ptr<ID3D11Texture2D> m_output;
    winrt::com_ptr<ID3D11RenderTargetView> m_outputRtv;
    winrt::com_ptr<ID3D11ShaderResourceView> m_outputSrv;
    UINT m_width = 0, m_height = 0;
};
