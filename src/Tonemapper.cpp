#include "Tonemapper.h"

namespace
{
    // Input is scRGB: linear BT.709 primaries where 1.0 = 80 nits. HDR highlights
    // go well above 1.0 and wide-gamut colors show up as negative components.
    // Output is linear 0..1 relative to SDR white, written to an sRGB render
    // target view so the GPU applies the sRGB transfer curve.
    constexpr char kTonemapPS[] = R"(
cbuffer Params : register(b0)
{
    float sdrWhiteNits;
    float peakNits;
    uint  mode;
    float pad;
};

Texture2D<float4> source : register(t0);
SamplerState linearClamp : register(s0);

// SMPTE ST 2084 (PQ), in absolute nits.
static const float m1 = 2610.0 / 16384.0;
static const float m2 = 2523.0 / 4096.0 * 128.0;
static const float c1 = 3424.0 / 4096.0;
static const float c2 = 2413.0 / 4096.0 * 32.0;
static const float c3 = 2392.0 / 4096.0 * 32.0;

float NitsToPQ(float nits)
{
    float y = pow(saturate(nits / 10000.0), m1);
    return pow((c1 + c2 * y) / (1.0 + c3 * y), m2);
}

float PQToNits(float e)
{
    float p = pow(saturate(e), 1.0 / m2);
    return 10000.0 * pow(max(p - c1, 0.0) / (c2 - c3 * p), 1.0 / m1);
}

// ITU-R BT.2390 EETF: leaves everything below the knee alone and rolls the
// rest off smoothly so that peakNits lands exactly on sdrWhiteNits.
float BT2390(float nits)
{
    float srcMax = NitsToPQ(peakNits);
    float e1 = min(NitsToPQ(nits) / srcMax, 1.0);
    float maxLum = NitsToPQ(sdrWhiteNits) / srcMax;
    float ks = max(1.5 * maxLum - 0.5, 0.0);
    float e2 = e1;
    if (e1 > ks)
    {
        float t = (e1 - ks) / (1.0 - ks);
        float t2 = t * t;
        float t3 = t2 * t;
        e2 = (2 * t3 - 3 * t2 + 1) * ks + (t3 - 2 * t2 + t) * (1 - ks) + (-2 * t3 + 3 * t2) * maxLum;
    }
    return PQToNits(e2 * srcMax);
}

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
    float3 nits = max(source.SampleLevel(linearClamp, uv, 0).rgb, 0.0) * 80.0;

    // Tonemap the brightest channel and scale RGB by the same ratio, which
    // keeps hue and saturation instead of washing highlights out to white.
    float peak = max(max(nits.r, nits.g), nits.b);
    if (peak > 0.0)
    {
        if (mode == 0 && peakNits > sdrWhiteNits)
        {
            nits *= BT2390(peak) / peak;
        }
        else if (mode == 1)
        {
            float x = peak / sdrWhiteNits;
            float w = max(peakNits / sdrWhiteNits, 1.0001);
            float y = x * (1.0 + x / (w * w)) / (1.0 + x);
            nits *= y * sdrWhiteNits / peak;
        }
    }
    return float4(saturate(nits / sdrWhiteNits), 1.0);
}
)";

    struct alignas(16) Constants
    {
        float sdrWhiteNits;
        float peakNits;
        UINT mode;
        float pad;
    };
}

void Tonemapper::Init(const Gpu& gpu)
{
    m_gpu = &gpu;
    auto ps = CompileShader(kTonemapPS, sizeof(kTonemapPS) - 1, "main", "ps_5_0");
    winrt::check_hresult(gpu.device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, m_ps.put()));

    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = sizeof(Constants);
    cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    winrt::check_hresult(gpu.device->CreateBuffer(&cb, nullptr, m_constants.put()));
}

void Tonemapper::EnsureTextures(DXGI_FORMAT sourceFormat, UINT width, UINT height)
{
    if (m_source && width == m_width && height == m_height)
        return;
    Clear();
    auto* device = m_gpu->device.get();

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = sourceFormat;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    winrt::check_hresult(device->CreateTexture2D(&desc, nullptr, m_source.put()));
    winrt::check_hresult(device->CreateShaderResourceView(m_source.get(), nullptr, m_sourceSrv.put()));

    // Typeless so we can write through an sRGB view (GPU encodes the curve) and
    // read through a UNORM view (consumers get the encoded 8-bit values as-is).
    desc.Format = DXGI_FORMAT_B8G8R8A8_TYPELESS;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    winrt::check_hresult(device->CreateTexture2D(&desc, nullptr, m_output.put()));

    D3D11_RENDER_TARGET_VIEW_DESC rtv{};
    rtv.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    winrt::check_hresult(device->CreateRenderTargetView(m_output.get(), &rtv, m_outputRtv.put()));

    D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;
    winrt::check_hresult(device->CreateShaderResourceView(m_output.get(), &srv, m_outputSrv.put()));

    m_width = width;
    m_height = height;
}

void Tonemapper::Process(ID3D11Texture2D* frame, UINT contentWidth, UINT contentHeight, const TonemapParams& params)
{
    D3D11_TEXTURE2D_DESC frameDesc{};
    frame->GetDesc(&frameDesc);
    UINT width = std::min(contentWidth, frameDesc.Width);
    UINT height = std::min(contentHeight, frameDesc.Height);
    if (width == 0 || height == 0)
        return;

    EnsureTextures(frameDesc.Format, width, height);

    // The pool's texture may be larger than the content after a resize; copy just the content.
    D3D11_BOX box{ 0, 0, 0, width, height, 1 };
    m_gpu->context->CopySubresourceRegion(m_source.get(), 0, 0, 0, 0, frame, 0, &box);
    Reprocess(params);
}

void Tonemapper::Reprocess(const TonemapParams& params)
{
    if (!m_sourceSrv)
        return;
    auto* ctx = m_gpu->context.get();

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(ctx->Map(m_constants.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        auto* c = static_cast<Constants*>(mapped.pData);
        c->sdrWhiteNits = std::max(params.sdrWhiteNits, 1.f);
        c->peakNits = std::max(params.peakNits, 1.f);
        c->mode = static_cast<UINT>(params.mode);
        c->pad = 0;
        ctx->Unmap(m_constants.get(), 0);
    }

    D3D11_VIEWPORT vp{ 0.f, 0.f, float(m_width), float(m_height), 0.f, 1.f };
    auto* rtv = m_outputRtv.get();
    auto* srv = m_sourceSrv.get();
    auto* cb = m_constants.get();
    auto* sampler = m_gpu->linearClamp.get();
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    ctx->RSSetViewports(1, &vp);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(m_gpu->fullscreenVS.get(), nullptr, 0);
    ctx->PSSetShader(m_ps.get(), nullptr, 0);
    ctx->PSSetConstantBuffers(0, 1, &cb);
    ctx->PSSetShaderResources(0, 1, &srv);
    ctx->PSSetSamplers(0, 1, &sampler);
    ctx->Draw(3, 0);

    ID3D11ShaderResourceView* nullSrv = nullptr;
    ctx->PSSetShaderResources(0, 1, &nullSrv);
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
}

void Tonemapper::Clear()
{
    m_sourceSrv = nullptr;
    m_source = nullptr;
    m_outputSrv = nullptr;
    m_outputRtv = nullptr;
    m_output = nullptr;
    m_width = m_height = 0;
}
