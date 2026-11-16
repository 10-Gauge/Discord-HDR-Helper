# Audio passthrough fix

Contributed by [@AndrixPro100GOD](https://github.com/AndrixPro100GOD) in 1.2.1, on top of v1.2.0.

## Problem and changes

Automatic playback set `ISimpleAudioVolume` to zero. Windows process loopback
captures session volume, so another application capturing the helper received
silence. Explicit device selection never restored the volume, and Windows could
reuse the silent session setting.

Playback now restores the helper session to full volume and unmutes it on
initialization, on both the default and explicitly selected endpoints. Its volume
is no longer persisted. The default option is labelled as audible replay; an
unused output or virtual cable is needed to avoid hearing a duplicate locally.

Capture startup and packet errors now include the failing operation and HRESULT.
Failed sources retry after five seconds (on the next source scan), instead of
being excluded until the configuration changes. Render errors restart the
pipeline. A configuration wake is consumed instead of repeatedly re-signalling
it, which previously caused the audio worker to spin.

## Automated verification

The opt-in test requires Windows 11/build 20348+, an active output endpoint, and
permission to play a quiet test tone. Close other audio applications for a clean
All apps result. It is intentionally excluded from ordinary CI, where WASAPI
hardware may be unavailable.

```powershell
cmake -S . -B build -A x64 -DHDR_HELPER_AUDIO_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure -V
```

The source and helper are sibling processes. A third process captures only the
helper's process tree; it cannot receive the source directly through that tree.
The test checks the helper's input meter and non-silent outgoing audio for the
default endpoint, an explicit endpoint, and All apps mode. It uses the production
WASAPI implementation and does not record audio to disk.

Locally verified on Windows 11 build 22631 with MSVC 19.44 and Windows SDK 26100:

- Release application and test build succeeded.
- All three output scenarios failed against the original implementation.
- All three output scenarios and input meter checks passed after the fix.
- The compiler still reports the pre-existing numeric conversion warning in
  `Panel.cpp`'s slider drawing code.

## Discord verification

The user tested the fixed executable on Windows 11 and reported that a remote
Discord viewer confirmed working audio. This confirms the reported no-sound
problem is resolved in that setup. The following steps cover further manual
checks; device switching and recovery are not claimed as viewer-verified.

1. Close the old helper and run the newly built executable.
2. Play sound in a game or media application. Capture its window, then try a
   display with All apps. Confirm the Audio meter moves.
3. Choose an unused output device if the replay is audible twice. Leave the
   helper unmuted in Windows Volume Mixer.
4. In Discord, share **Discord HDR Helper Output** under **Applications**, with
   **Sound** enabled. Have a viewer confirm audio.
5. Switch playback devices and turn audio off and on. Verify sound resumes.
6. If the meter stays at zero, report the complete Sources/status text, including
   the operation and HRESULT if present, plus the source app and Windows build.

The automated Windows tests establish that audio crosses the helper process;
remote audio was confirmed separately by the user and their viewer. The user's
original zero input meter was not independently reproduced; improved error
reporting makes a remaining source-specific failure diagnosable.
