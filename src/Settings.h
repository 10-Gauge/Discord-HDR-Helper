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

    void Load();
    void Save() const;
};
