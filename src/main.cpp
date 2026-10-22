#include "pch.h"
#include "App.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCmd)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    winrt::init_apartment(winrt::apartment_type::single_threaded);

    // Only one copy at a time; a second launch brings the first one forward.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"DiscordHDRHelper.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (HWND existing = FindWindowW(L"DiscordHDRHelperPanel", nullptr))
        {
            ShowWindow(existing, IsIconic(existing) ? SW_RESTORE : SW_SHOW);
            SetForegroundWindow(existing);
        }
        return 0;
    }

    // Hidden option used by CI to produce a screenshot of the UI.
    std::wstring screenshotPath;
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc))
    {
        constexpr std::wstring_view flag = L"--screenshot=";
        for (int i = 1; i < argc; ++i)
            if (std::wstring_view(argv[i]).starts_with(flag))
                screenshotPath = argv[i] + flag.size();
        LocalFree(argv);
    }

    if (screenshotPath.empty() && !winrt::Windows::Graphics::Capture::GraphicsCaptureSession::IsSupported())
    {
        MessageBoxW(nullptr, L"Screen capture isn't supported on this version of Windows.\n"
                             L"Discord HDR Helper needs Windows 10 version 2004 or newer.",
                    L"Discord HDR Helper", MB_ICONERROR);
        return 1;
    }

    int result = 1;
    try
    {
        App app;
        result = app.Run(instance, showCmd, screenshotPath);
    }
    catch (winrt::hresult_error const& e)
    {
        std::wstring message = L"Discord HDR Helper hit an error and has to close:\n\n" + std::wstring(e.message());
        MessageBoxW(nullptr, message.c_str(), L"Discord HDR Helper", MB_ICONERROR);
    }

    if (mutex)
        CloseHandle(mutex);
    return result;
}
