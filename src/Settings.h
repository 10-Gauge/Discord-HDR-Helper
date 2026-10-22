#pragma once
#include "pch.h"

enum class TonemapMode : int
{
    BT2390 = 0,  // Keeps SDR-range content as-is, rolls off highlights (default)
    Reinhard = 1, // Softer, compresses the whole range a little
    Clip = 2,     // No highlight compression, just brightness correction
};

struct Settings
{
    TonemapMode mode = TonemapMode::BT2390;
    int sdrWhiteNits = 0; // 0 = follow Windows' "SDR content brightness"
    int peakNits = 0;     // 0 = use the display's reported peak
    bool captureCursor = true;
    bool outputInBackground = true; // Keep the output window under all other windows
    bool hidePanelFromCapture = true; // Keep the control panel out of the stream
    int streamLevel = 0; // Output level in percent to offset Discord's brightening; 0 = auto (80 / SDR white)

    bool audioEnabled = true;   // Replay app audio so Discord's window share carries it
    int audioMode = 0;          // 0 = the captured window's app (all apps for a display), 1 = all apps
    std::wstring audioDevice;   // Output device to replay into; empty = automatic (silent on the default device)

    void Load();
    void Save() const;
};
