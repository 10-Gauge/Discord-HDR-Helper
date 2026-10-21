#pragma once
#include "pch.h"

#include <mutex>
#include <thread>

// Captures app audio with WASAPI process loopback and plays it from this
// process, so that Discord's "share a window" audio (which only takes sound
// from the shared window's app) includes it. Runs on its own MTA thread.
class AudioPassthrough
{
public:
    struct Device
    {
        std::wstring id;
        std::wstring name;
        bool isDefault = false;
    };

    struct Status
    {
        bool running = false;
        std::wstring error;
        std::vector<std::wstring> sources; // Executable names currently captured
        float level = 0.f;                 // Recent peak, 0..1
    };

    AudioPassthrough();
    ~AudioPassthrough();

    AudioPassthrough(const AudioPassthrough&) = delete;
    AudioPassthrough& operator=(const AudioPassthrough&) = delete;

    static std::vector<Device> EnumerateOutputDevices();

    // targetProcess = 0 captures every app except Discord and this one.
    // An empty deviceId plays silently on the default device.
    void Configure(bool enabled, const std::wstring& deviceId, DWORD targetProcess);
    Status GetStatus() const;

private:
    struct Config
    {
        bool enabled = false;
        std::wstring deviceId;
        DWORD targetProcess = 0;
        bool operator==(const Config&) const = default;
    };

    void ThreadMain();
    void RunPipeline(const Config& config, uint64_t generation);

    std::thread m_thread;
    mutable std::mutex m_mutex;
    Config m_config;
    uint64_t m_generation = 0;
    std::atomic<bool> m_stop = false;
    HANDLE m_wake = nullptr;
    Status m_status;
};
