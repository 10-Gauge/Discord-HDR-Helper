#pragma once
#include "pch.h"

struct DisplayInfo
{
    HMONITOR monitor = nullptr;
    std::wstring deviceName;  // e.g. \\.\DISPLAY1
    std::wstring friendlyName; // e.g. "LG ULTRAGEAR", may be empty
    RECT bounds{};
    bool primary = false;
    bool hdr = false;          // Windows HDR is turned on for this display
    float sdrWhiteNits = 80.f; // "SDR content brightness" slider in Windows settings
    float peakNits = 1000.f;   // Display's reported peak luminance
};

// All active displays, primary first.
std::vector<DisplayInfo> EnumerateDisplays();

// Info for the display that contains (most of) the given window or monitor.
DisplayInfo QueryDisplay(HMONITOR monitor);
