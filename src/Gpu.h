#pragma once
#include "pch.h"

// The one D3D11 device shared by capture, tonemapping, the output window and the UI.
struct Gpu
{
    winrt::com_ptr<ID3D11Device> device;
    winrt::com_ptr<ID3D11DeviceContext> context;
    winrt::com_ptr<IDXGIFactory2> factory;
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice winrtDevice{ nullptr };

    void Init();

    // Flip-model 8-bit swap chain for a window.
    winrt::com_ptr<IDXGISwapChain1> CreateSwapChain(HWND hwnd) const;

    // Fullscreen-triangle vertex shader shared by every pass.
    winrt::com_ptr<ID3D11VertexShader> fullscreenVS;
    winrt::com_ptr<ID3D11SamplerState> linearClamp;
};

winrt::com_ptr<ID3DBlob> CompileShader(const char* source, size_t size, const char* entry, const char* target);
