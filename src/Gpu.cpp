#include "Gpu.h"

namespace
{
    constexpr char kFullscreenVS[] = R"(
struct VSOut
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

VSOut main(uint id : SV_VertexID)
{
    // One triangle that covers the whole viewport.
    VSOut o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
)";
}

winrt::com_ptr<ID3DBlob> CompileShader(const char* source, size_t size, const char* entry, const char* target)
{
    winrt::com_ptr<ID3DBlob> code, errors;
    HRESULT hr = D3DCompile(source, size, nullptr, nullptr, nullptr, entry, target,
                            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, code.put(), errors.put());
    if (FAILED(hr))
    {
        if (errors)
            OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
        winrt::throw_hresult(hr);
    }
    return code;
}

void Gpu::Init()
{
    // BGRA support is required by both Windows.Graphics.Capture and Direct2D.
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, ARRAYSIZE(levels),
                                   D3D11_SDK_VERSION, device.put(), nullptr, context.put());
    if (FAILED(hr))
    {
        // No usable GPU (e.g. a VM or remote session): fall back to the WARP software rasterizer.
        winrt::check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, ARRAYSIZE(levels),
                                               D3D11_SDK_VERSION, device.put(), nullptr, context.put()));
    }

    // The capture frame pool uses the device from its own thread.
    if (auto mt = device.try_as<ID3D11Multithread>())
        mt->SetMultithreadProtected(TRUE);

    auto dxgiDevice = device.as<IDXGIDevice>();
    winrt::com_ptr<::IInspectable> inspectable;
    winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.get(), inspectable.put()));
    winrtDevice = inspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();

    winrt::com_ptr<IDXGIAdapter> adapter;
    winrt::check_hresult(dxgiDevice->GetAdapter(adapter.put()));
    winrt::check_hresult(adapter->GetParent(IID_PPV_ARGS(factory.put())));

    auto vs = CompileShader(kFullscreenVS, sizeof(kFullscreenVS) - 1, "main", "vs_5_0");
    winrt::check_hresult(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, fullscreenVS.put()));

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    winrt::check_hresult(device->CreateSamplerState(&sd, linearClamp.put()));
}

winrt::com_ptr<IDXGISwapChain1> Gpu::CreateSwapChain(HWND hwnd, DXGI_FORMAT format) const
{
    RECT rc{};
    GetClientRect(hwnd, &rc);

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = std::max<LONG>(rc.right - rc.left, 1);
    desc.Height = std::max<LONG>(rc.bottom - rc.top, 1);
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    desc.Scaling = DXGI_SCALING_STRETCH; // Buffers may be larger than the window (see OutputWindow::Resize)

    winrt::com_ptr<IDXGISwapChain1> swapChain;
    winrt::check_hresult(factory->CreateSwapChainForHwnd(device.get(), hwnd, &desc, nullptr, nullptr, swapChain.put()));
    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    return swapChain;
}
