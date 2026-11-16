// Opt-in Windows hardware test. A quiet tone source and the helper run as
// siblings, so capturing the helper's process tree cannot capture the tone
// directly. No microphone or audio files are used.
#include "../src/AudioPassthrough.cpp"
#include <iostream>

namespace
{
    struct Child
    {
        winrt::handle process;
        explicit Child(const std::wstring& args)
        {
            wchar_t path[MAX_PATH]{};
            GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
            std::wstring command = L"\"" + std::wstring(path) + L"\" " + args;
            STARTUPINFOW startup{ sizeof(startup) };
            startup.dwFlags = STARTF_USESHOWWINDOW;
            startup.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION info{};
            winrt::check_bool(CreateProcessW(nullptr, command.data(), nullptr, nullptr,
                FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info));
            process.attach(info.hProcess);
            CloseHandle(info.hThread);
        }
        DWORD Pid() const { return GetProcessId(process.get()); }
        ~Child()
        {
            // Only terminate the synthetic child owned by this test.
            if (WaitForSingleObject(process.get(), 0) == WAIT_TIMEOUT)
            {
                TerminateProcess(process.get(), 0);
                WaitForSingleObject(process.get(), 2000);
            }
        }
    };

    void Tone()
    {
        auto enumerator = winrt::create_instance<IMMDeviceEnumerator>(__uuidof(MMDeviceEnumerator));
        winrt::com_ptr<IMMDevice> device;
        winrt::check_hresult(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, device.put()));
        winrt::com_ptr<IAudioClient> render;
        winrt::check_hresult(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, render.put_void()));
        auto format = FloatFormat();
        GUID session;
        winrt::check_hresult(CoCreateGuid(&session));
        winrt::check_hresult(render->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_NOPERSIST,
            kBufferDuration, 0, &format, &session));
        winrt::com_ptr<IAudioRenderClient> output;
        winrt::check_hresult(render->GetService(IID_PPV_ARGS(output.put())));
        UINT32 size = 0;
        winrt::check_hresult(render->GetBufferSize(&size));
        winrt::check_hresult(render->Start());
        uint64_t sample = 0;
        auto started = GetTickCount64();
        while (GetTickCount64() - started < 30000)
        {
            UINT32 padding = 0;
            winrt::check_hresult(render->GetCurrentPadding(&padding));
            if (size > padding)
            {
                BYTE* buffer = nullptr;
                winrt::check_hresult(output->GetBuffer(size - padding, &buffer));
                auto dst = reinterpret_cast<float*>(buffer);
                for (UINT32 i = 0; i < size - padding; ++i, ++sample)
                    dst[2*i] = dst[2*i+1] = .01f * std::sin(float(sample % kSampleRate) * 440.f * 6.2831853f / kSampleRate);
                winrt::check_hresult(output->ReleaseBuffer(size - padding, 0));
            }
            Sleep(5);
        }
        render->Stop();
    }

    void Relay(DWORD source, bool explicitDevice, bool allApps)
    {
        std::wstring deviceId;
        if (explicitDevice)
            for (const auto& device : AudioPassthrough::EnumerateOutputDevices())
                if (device.isDefault) deviceId = device.id;
        if (explicitDevice && deviceId.empty())
            throw winrt::hresult_error(E_FAIL, L"No default output device");
        AudioPassthrough audio;
        audio.Configure(true, deviceId, allApps ? 0 : source);
        float peak = 0;
        auto started = GetTickCount64();
        while (GetTickCount64() - started < 4500)
        {
            const auto status = audio.GetStatus();
            if (!status.error.empty()) throw winrt::hresult_error(E_FAIL, status.error);
            peak = std::max(peak, status.level);
            Sleep(20);
        }
        if (peak < .002f) throw winrt::hresult_error(E_FAIL, L"Helper's input meter stayed at zero");
    }

    float Measure(ProcessCapture& capture, DWORD duration)
    {
        float peak = 0;
        auto started = GetTickCount64();
        while (GetTickCount64() - started < duration)
        {
            capture.Drain();
            for (float value : capture.queue) peak = std::max(peak, std::abs(value));
            capture.queue.clear();
            Sleep(5);
        }
        return peak;
    }
}

int wmain(int argc, wchar_t** argv)
{
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    try
    {
        if (argc == 2 && std::wstring_view(argv[1]) == L"--source") { Tone(); return 0; }
        if (argc == 5 && std::wstring_view(argv[1]) == L"--relay")
        {
            Relay(std::stoul(argv[2]), std::stoi(argv[3]) != 0, std::stoi(argv[4]) != 0);
            return 0;
        }
        Child source(L"--source");
        ProcessCapture sourceCapture;
        if (!sourceCapture.Start(source.Pid())) throw winrt::hresult_error(E_FAIL, L"Source capture failed");
        const float sourcePeak = Measure(sourceCapture, 1500);
        std::cout << "Source peak: " << sourcePeak << std::endl;
        if (sourcePeak < .002f) throw winrt::hresult_error(E_FAIL, L"Test tone unavailable");
        bool passed = true;
        for (int mode = 0; mode < 3; ++mode)
        {
            Child relay(L"--relay " + std::to_wstring(source.Pid()) + (mode == 1 ? L" 1" : L" 0") + (mode == 2 ? L" 1" : L" 0"));
            ProcessCapture observer;
            if (!observer.Start(relay.Pid())) throw winrt::hresult_error(E_FAIL, L"Relay capture failed");
            float peak = Measure(observer, 3500);
            DWORD exitCode = STILL_ACTIVE;
            WaitForSingleObject(relay.process.get(), 3000);
            winrt::check_bool(GetExitCodeProcess(relay.process.get(), &exitCode));
            bool ok = peak > .002f && exitCode == 0;
            std::cout << (mode == 0 ? "Default / captured app" : mode == 1 ? "Explicit / captured app" : "Default / all apps")
                << ": peak=" << peak << (ok ? " PASS" : " FAIL") << std::endl;
            passed &= ok;
        }
        return passed ? 0 : 1;
    }
    catch (winrt::hresult_error const& e)
    {
        std::cerr << "Audio test failed: 0x" << std::hex << unsigned(e.code()) << " " << winrt::to_string(e.message()) << std::endl;
        return 1;
    }
}
