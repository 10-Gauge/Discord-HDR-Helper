#include "Display.h"

namespace
{
    // Fills the SDR white level and the monitor's friendly name from the display config.
    void FillFromDisplayConfig(DisplayInfo& info)
    {
        UINT32 pathCount = 0, modeCount = 0;
        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS)
            return;

        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
        if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) != ERROR_SUCCESS)
            return;

        for (UINT32 i = 0; i < pathCount; ++i)
        {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
            source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
            source.header.size = sizeof(source);
            source.header.adapterId = paths[i].sourceInfo.adapterId;
            source.header.id = paths[i].sourceInfo.id;
            if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS)
                continue;
            if (info.deviceName != source.viewGdiDeviceName)
                continue;

            // Windows stores the SDR white level as a multiple of 80 nits, times 1000.
            DISPLAYCONFIG_SDR_WHITE_LEVEL white{};
            white.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
            white.header.size = sizeof(white);
            white.header.adapterId = paths[i].targetInfo.adapterId;
            white.header.id = paths[i].targetInfo.id;
            if (DisplayConfigGetDeviceInfo(&white.header) == ERROR_SUCCESS)
                info.sdrWhiteNits = white.SDRWhiteLevel / 1000.f * 80.f;

            DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
            target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
            target.header.size = sizeof(target);
            target.header.adapterId = paths[i].targetInfo.adapterId;
            target.header.id = paths[i].targetInfo.id;
            if (DisplayConfigGetDeviceInfo(&target.header) == ERROR_SUCCESS)
                info.friendlyName = target.monitorFriendlyDeviceName;
            return;
        }
    }

    void FillFromDxgi(DisplayInfo& info)
    {
        winrt::com_ptr<IDXGIFactory1> factory;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory.put()))))
            return;

        winrt::com_ptr<IDXGIAdapter1> adapter;
        for (UINT a = 0; factory->EnumAdapters1(a, adapter.put()) != DXGI_ERROR_NOT_FOUND; ++a)
        {
            winrt::com_ptr<IDXGIOutput> output;
            for (UINT o = 0; adapter->EnumOutputs(o, output.put()) != DXGI_ERROR_NOT_FOUND; ++o)
            {
                auto output6 = output.try_as<IDXGIOutput6>();
                DXGI_OUTPUT_DESC1 desc{};
                if (output6 && SUCCEEDED(output6->GetDesc1(&desc)) && desc.Monitor == info.monitor)
                {
                    info.hdr = desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
                    if (desc.MaxLuminance > 0.f)
                        info.peakNits = desc.MaxLuminance;
                    return;
                }
                output = nullptr;
            }
            adapter = nullptr;
        }
    }
}

DisplayInfo QueryDisplay(HMONITOR monitor)
{
    DisplayInfo info;
    info.monitor = monitor;

    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(monitor, &mi))
    {
        info.deviceName = mi.szDevice;
        info.bounds = mi.rcMonitor;
        info.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
        FillFromDisplayConfig(info);
    }
    FillFromDxgi(info);
    return info;
}

std::vector<DisplayInfo> EnumerateDisplays()
{
    std::vector<HMONITOR> monitors;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR m, HDC, LPRECT, LPARAM lp) -> BOOL {
        reinterpret_cast<std::vector<HMONITOR>*>(lp)->push_back(m);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&monitors));

    std::vector<DisplayInfo> displays;
    for (auto m : monitors)
        displays.push_back(QueryDisplay(m));

    std::stable_sort(displays.begin(), displays.end(), [](const DisplayInfo& a, const DisplayInfo& b) {
        return a.primary && !b.primary;
    });
    return displays;
}
