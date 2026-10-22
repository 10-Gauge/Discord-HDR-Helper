#include "AudioPassthrough.h"

#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <audiopolicy.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>
#include <tlhelp32.h>

#include <deque>
#include <map>
#include <set>

namespace
{
    constexpr UINT32 kSampleRate = 48000;
    constexpr UINT32 kChannels = 2;
    constexpr REFERENCE_TIME kBufferDuration = 400000; // 40 ms, in 100 ns units
    constexpr size_t kPrimeFrames = kSampleRate / 50;   // 20 ms before a source starts playing
    constexpr size_t kMaxQueuedFrames = kSampleRate / 10; // Drop backlog beyond 100 ms

    // Waits for ActivateAudioInterfaceAsync. C++/WinRT makes it agile, as required.
    struct ActivationHandler : winrt::implements<ActivationHandler, IActivateAudioInterfaceCompletionHandler>
    {
        winrt::handle done{ CreateEventW(nullptr, TRUE, FALSE, nullptr) };

        HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation*) override
        {
            SetEvent(done.get());
            return S_OK;
        }
    };

    WAVEFORMATEX PcmFormat()
    {
        WAVEFORMATEX f{};
        f.wFormatTag = WAVE_FORMAT_PCM;
        f.nChannels = kChannels;
        f.nSamplesPerSec = kSampleRate;
        f.wBitsPerSample = 16;
        f.nBlockAlign = f.nChannels * f.wBitsPerSample / 8;
        f.nAvgBytesPerSec = f.nSamplesPerSec * f.nBlockAlign;
        return f;
    }

    WAVEFORMATEX FloatFormat()
    {
        WAVEFORMATEX f{};
        f.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        f.nChannels = kChannels;
        f.nSamplesPerSec = kSampleRate;
        f.wBitsPerSample = 32;
        f.nBlockAlign = f.nChannels * f.wBitsPerSample / 8;
        f.nAvgBytesPerSec = f.nSamplesPerSec * f.nBlockAlign;
        return f;
    }

    // Loopback capture of one process and everything it spawned.
    struct ProcessCapture
    {
        DWORD pid = 0;
        winrt::com_ptr<IAudioClient> client;
        winrt::com_ptr<IAudioCaptureClient> capture;
        winrt::handle event{ CreateEventW(nullptr, FALSE, FALSE, nullptr) };
        std::deque<float> queue; // Interleaved stereo samples
        bool primed = false;

        // Diagnostics for the status line.
        ULONGLONG startedAt = GetTickCount64();
        ULONGLONG lastPacketAt = 0; // Any data from Windows, even silence
        ULONGLONG lastSoundAt = 0;  // Data with at least one non-zero sample
        HRESULT lastError = S_OK;

        // How this capture is doing, for the status line; empty when sound is flowing.
        std::wstring Health() const
        {
            ULONGLONG now = GetTickCount64();
            if (FAILED(lastError))
            {
                wchar_t text[48];
                swprintf_s(text, L"error 0x%08X", unsigned(lastError));
                return text;
            }
            if (now - startedAt < 3000 || now - lastSoundAt < 3000)
                return {};
            if (now - lastPacketAt < 3000)
                return L"getting only silence";
            return L"no data from Windows";
        }

        bool Start(DWORD processId)
        {
            pid = processId;
            AUDIOCLIENT_ACTIVATION_PARAMS params{};
            params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
            params.ProcessLoopbackParams.TargetProcessId = processId;
            params.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
            PROPVARIANT pv{};
            pv.vt = VT_BLOB;
            pv.blob.cbSize = sizeof(params);
            pv.blob.pBlobData = reinterpret_cast<BYTE*>(&params);

            auto handler = winrt::make_self<ActivationHandler>();
            winrt::com_ptr<IActivateAudioInterfaceAsyncOperation> op;
            if (FAILED(ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &pv,
                                                   handler.get(), op.put())))
                return false;
            if (WaitForSingleObject(handler->done.get(), 5000) != WAIT_OBJECT_0)
                return false;

            HRESULT activateResult = E_FAIL;
            winrt::com_ptr<::IUnknown> unknown;
            if (FAILED(op->GetActivateResult(&activateResult, unknown.put())) || FAILED(activateResult) || !unknown)
                return false;
            client = unknown.as<IAudioClient>();

            // Process loopback has no mix format of its own; we ask for 48 kHz 16-bit stereo.
            WAVEFORMATEX format = PcmFormat();
            DWORD flags = AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM;
            if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, kBufferDuration * 5, 0, &format, nullptr)))
                return false;
            if (FAILED(client->SetEventHandle(event.get())))
                return false;
            if (FAILED(client->GetService(IID_PPV_ARGS(capture.put()))))
                return false;
            return SUCCEEDED(client->Start());
        }

        void Drain()
        {
            // Read packets until the buffer is empty. GetBuffer is used directly
            // rather than GetNextPacketSize, which some builds report as 0.
            for (int guard = 0; guard < 64; ++guard)
            {
                BYTE* data = nullptr;
                UINT32 frames = 0;
                DWORD flags = 0;
                HRESULT hr = capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
                if (FAILED(hr))
                {
                    lastError = hr;
                    break;
                }
                if (hr == AUDCLNT_S_BUFFER_EMPTY || frames == 0)
                    break;
                ULONGLONG now = GetTickCount64();
                lastPacketAt = now;
                const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) || !data;
                auto* samples = reinterpret_cast<const int16_t*>(data);
                bool sound = false;
                for (UINT32 i = 0; i < frames * kChannels; ++i)
                {
                    int16_t v = silent ? 0 : samples[i];
                    sound |= v != 0;
                    queue.push_back(v / 32768.f);
                }
                if (sound)
                    lastSoundAt = now;
                capture->ReleaseBuffer(frames);
            }

            // Keep latency bounded if playback can't keep up.
            size_t frames = queue.size() / kChannels;
            if (frames > kMaxQueuedFrames)
                queue.erase(queue.begin(), queue.begin() + static_cast<std::ptrdiff_t>((frames - kPrimeFrames) * kChannels));
            if (!primed && queue.size() / kChannels >= kPrimeFrames)
                primed = true;
        }

        ~ProcessCapture()
        {
            if (client)
                client->Stop();
        }
    };

    std::wstring ExeName(DWORD pid)
    {
        std::wstring name;
        if (HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid))
        {
            wchar_t path[MAX_PATH] = {};
            DWORD size = MAX_PATH;
            if (QueryFullProcessImageNameW(process, 0, path, &size))
            {
                std::wstring p(path, size);
                name = p.substr(p.find_last_of(L'\\') + 1);
            }
            CloseHandle(process);
        }
        return name;
    }

    std::map<DWORD, DWORD> ParentMap()
    {
        std::map<DWORD, DWORD> parents;
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
            return parents;
        PROCESSENTRY32W entry{ sizeof(entry) };
        for (BOOL ok = Process32FirstW(snapshot, &entry); ok; ok = Process32NextW(snapshot, &entry))
            parents[entry.th32ProcessID] = entry.th32ParentProcessID;
        CloseHandle(snapshot);
        return parents;
    }

    // Every process with an audio session on any output device, minus system
    // sounds, Discord and ourselves. A process whose parent is also in the set
    // is dropped, since capturing the parent's tree already includes it.
    std::set<DWORD> AudioProcesses()
    {
        std::set<DWORD> pids;
        winrt::com_ptr<IMMDeviceEnumerator> enumerator;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(enumerator.put()))))
            return pids;
        winrt::com_ptr<IMMDeviceCollection> devices;
        if (FAILED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, devices.put())))
            return pids;

        UINT deviceCount = 0;
        devices->GetCount(&deviceCount);
        for (UINT d = 0; d < deviceCount; ++d)
        {
            winrt::com_ptr<IMMDevice> device;
            winrt::com_ptr<IAudioSessionManager2> manager;
            winrt::com_ptr<IAudioSessionEnumerator> sessions;
            if (FAILED(devices->Item(d, device.put())) ||
                FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, manager.put_void())) ||
                FAILED(manager->GetSessionEnumerator(sessions.put())))
                continue;

            int sessionCount = 0;
            sessions->GetCount(&sessionCount);
            for (int s = 0; s < sessionCount; ++s)
            {
                winrt::com_ptr<IAudioSessionControl> control;
                if (FAILED(sessions->GetSession(s, control.put())))
                    continue;
                auto control2 = control.try_as<IAudioSessionControl2>();
                AudioSessionState state = AudioSessionStateExpired;
                DWORD pid = 0;
                if (!control2 || control2->IsSystemSoundsSession() == S_OK || FAILED(control2->GetState(&state)) ||
                    state == AudioSessionStateExpired || FAILED(control2->GetProcessId(&pid)) || pid == 0)
                    continue;
                pids.insert(pid);
            }
        }

        DWORD self = GetCurrentProcessId();
        std::erase_if(pids, [self](DWORD pid) {
            if (pid == self)
                return true;
            auto name = ExeName(pid);
            return _wcsnicmp(name.c_str(), L"Discord", 7) == 0;
        });

        auto parents = ParentMap();
        std::set<DWORD> roots;
        for (DWORD pid : pids)
        {
            bool covered = false;
            DWORD cur = pid;
            for (int depth = 0; depth < 32; ++depth)
            {
                auto it = parents.find(cur);
                if (it == parents.end() || it->second == 0 || it->second == cur)
                    break;
                cur = it->second;
                if (pids.count(cur))
                {
                    covered = true;
                    break;
                }
            }
            if (!covered)
                roots.insert(pid);
        }
        return roots;
    }
}

AudioPassthrough::AudioPassthrough()
{
    m_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    m_thread = std::thread([this] { ThreadMain(); });
}

AudioPassthrough::~AudioPassthrough()
{
    m_stop = true;
    SetEvent(m_wake);
    if (m_thread.joinable())
        m_thread.join();
    CloseHandle(m_wake);
}

std::vector<AudioPassthrough::Device> AudioPassthrough::EnumerateOutputDevices()
{
    std::vector<Device> result;
    winrt::com_ptr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(enumerator.put()))))
        return result;

    std::wstring defaultId;
    winrt::com_ptr<IMMDevice> defaultDevice;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, defaultDevice.put())))
    {
        LPWSTR id = nullptr;
        if (SUCCEEDED(defaultDevice->GetId(&id)))
            defaultId = id;
        CoTaskMemFree(id);
    }

    winrt::com_ptr<IMMDeviceCollection> devices;
    if (FAILED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, devices.put())))
        return result;
    UINT count = 0;
    devices->GetCount(&count);
    for (UINT i = 0; i < count; ++i)
    {
        winrt::com_ptr<IMMDevice> device;
        if (FAILED(devices->Item(i, device.put())))
            continue;
        Device d;
        LPWSTR id = nullptr;
        if (SUCCEEDED(device->GetId(&id)))
            d.id = id;
        CoTaskMemFree(id);

        winrt::com_ptr<IPropertyStore> props;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, props.put())))
        {
            PROPVARIANT name;
            PropVariantInit(&name);
            if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &name)) && name.vt == VT_LPWSTR)
                d.name = name.pwszVal;
            PropVariantClear(&name);
        }
        d.isDefault = d.id == defaultId;
        result.push_back(std::move(d));
    }
    return result;
}

void AudioPassthrough::Configure(bool enabled, const std::wstring& deviceId, DWORD targetProcess)
{
    Config config{ enabled, deviceId, targetProcess };
    std::lock_guard lock(m_mutex);
    if (config == m_config)
        return;
    m_config = config;
    ++m_generation;
    SetEvent(m_wake);
}

AudioPassthrough::Status AudioPassthrough::GetStatus() const
{
    std::lock_guard lock(m_mutex);
    return m_status;
}

void AudioPassthrough::ThreadMain()
{
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    while (!m_stop)
    {
        Config config;
        uint64_t generation;
        {
            std::lock_guard lock(m_mutex);
            config = m_config;
            generation = m_generation;
        }

        if (config.enabled)
        {
            try
            {
                RunPipeline(config, generation);
            }
            catch (winrt::hresult_error const& e)
            {
                std::lock_guard lock(m_mutex);
                m_status = {};
                m_status.error = std::wstring(e.message());
            }
        }
        else
        {
            std::lock_guard lock(m_mutex);
            m_status = {};
        }

        // Wait for a config change (or retry a failed pipeline after a moment).
        bool changed;
        {
            std::lock_guard lock(m_mutex);
            changed = generation != m_generation;
        }
        if (!changed && !m_stop)
            WaitForSingleObject(m_wake, 2000);
    }
    winrt::uninit_apartment();
}

void AudioPassthrough::RunPipeline(const Config& config, uint64_t generation)
{
    // Playback on the chosen device.
    winrt::com_ptr<IMMDeviceEnumerator> enumerator;
    winrt::check_hresult(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(enumerator.put())));
    // No device chosen = "automatic": play on the default device with this
    // stream's volume at zero, relying on loopback capture seeing the stream
    // before its volume is applied.
    const bool automatic = config.deviceId.empty();
    winrt::com_ptr<IMMDevice> device;
    HRESULT hr = automatic ? enumerator->GetDefaultAudioEndpoint(eRender, eConsole, device.put())
                           : enumerator->GetDevice(config.deviceId.c_str(), device.put());
    if (FAILED(hr))
        throw winrt::hresult_error(E_FAIL, L"The selected output device isn't available.");

    winrt::com_ptr<IAudioClient> render;
    winrt::check_hresult(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, render.put_void()));
    WAVEFORMATEX format = FloatFormat();
    winrt::check_hresult(render->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                            AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                                AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                            kBufferDuration, 0, &format, nullptr));
    winrt::handle renderEvent{ CreateEventW(nullptr, FALSE, FALSE, nullptr) };
    winrt::check_hresult(render->SetEventHandle(renderEvent.get()));
    winrt::com_ptr<IAudioRenderClient> renderClient;
    winrt::check_hresult(render->GetService(IID_PPV_ARGS(renderClient.put())));
    if (automatic)
    {
        winrt::com_ptr<ISimpleAudioVolume> volume;
        if (SUCCEEDED(render->GetService(IID_PPV_ARGS(volume.put()))))
            volume->SetMasterVolume(0.f, nullptr);
    }
    UINT32 bufferFrames = 0;
    winrt::check_hresult(render->GetBufferSize(&bufferFrames));
    winrt::check_hresult(render->Start());

    std::map<DWORD, std::unique_ptr<ProcessCapture>> captures;
    std::set<DWORD> failed; // Processes whose capture couldn't start; not retried
    ULONGLONG lastScan = 0;
    std::vector<float> mix;

    while (!m_stop)
    {
        {
            std::lock_guard lock(m_mutex);
            if (generation != m_generation)
                break;
        }

        // Track which apps to capture.
        ULONGLONG now = GetTickCount64();
        if (now - lastScan >= 1500)
        {
            lastScan = now;
            std::set<DWORD> wanted = config.targetProcess ? std::set<DWORD>{ config.targetProcess } : AudioProcesses();
            std::erase_if(captures, [&](auto& kv) { return !wanted.count(kv.first); });
            for (DWORD pid : wanted)
            {
                if (captures.count(pid) || failed.count(pid))
                    continue;
                auto capture = std::make_unique<ProcessCapture>();
                if (capture->Start(pid))
                    captures.emplace(pid, std::move(capture));
                else
                    failed.insert(pid);
            }

            std::vector<std::wstring> sources;
            for (auto& kv : captures)
            {
                std::wstring name = ExeName(kv.first);
                auto health = kv.second->Health();
                if (!health.empty())
                    name += L" (" + health + L")";
                sources.push_back(std::move(name));
            }
            for (DWORD pid : failed)
                if (wanted.count(pid))
                    sources.push_back(ExeName(pid) + L" (Windows refused to capture it)");
            std::lock_guard lock(m_mutex);
            m_status.running = true;
            m_status.error.clear();
            m_status.sources = std::move(sources);
        }

        HANDLE waits[] = { renderEvent.get(), m_wake };
        DWORD wait = WaitForMultipleObjects(2, waits, FALSE, 100);
        if (wait == WAIT_OBJECT_0 + 1)
            SetEvent(m_wake); // Let the outer loop see the change too.

        for (auto& kv : captures)
            kv.second->Drain();

        UINT32 padding = 0;
        if (FAILED(render->GetCurrentPadding(&padding)))
            throw winrt::hresult_error(E_FAIL, L"The output device stopped working.");
        UINT32 frames = bufferFrames - padding;
        if (frames == 0)
            continue;

        mix.assign(size_t(frames) * kChannels, 0.f);
        for (auto& kv : captures)
        {
            ProcessCapture& c = *kv.second;
            if (!c.primed)
                continue;
            size_t n = std::min(mix.size(), c.queue.size());
            for (size_t i = 0; i < n; ++i)
                mix[i] += c.queue[i];
            c.queue.erase(c.queue.begin(), c.queue.begin() + static_cast<std::ptrdiff_t>(n));
            if (c.queue.empty())
                c.primed = false; // Ran dry: re-buffer instead of crackling.
        }

        BYTE* out = nullptr;
        if (FAILED(renderClient->GetBuffer(frames, &out)))
            continue;
        auto* dst = reinterpret_cast<float*>(out);
        float peak = 0.f;
        for (size_t i = 0; i < mix.size(); ++i)
        {
            float v = std::clamp(mix[i], -1.f, 1.f);
            dst[i] = v;
            peak = std::max(peak, std::abs(v));
        }
        renderClient->ReleaseBuffer(frames, 0);

        {
            std::lock_guard lock(m_mutex);
            m_status.level = std::max(m_status.level * 0.85f, peak);
        }
    }

    render->Stop();
    std::lock_guard lock(m_mutex);
    m_status = {};
}
