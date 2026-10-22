#pragma once

// Classic Win32/COM headers must come before C++/WinRT so that winrt::com_ptr
// can interoperate with classic COM interfaces (requires <unknwn.h> first).
#include <windows.h>
#include <windowsx.h>
#include <unknwn.h>
#include <inspectable.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>

#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <d2d1_1.h>
#include <dcomp.h>
#include <dwrite.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <string>
#include <string_view>
#include <vector>
