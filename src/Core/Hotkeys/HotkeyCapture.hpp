#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>

namespace HotkeyCapture {

    /// Past every other WM_APP message the windows that host a capture handle.
    inline constexpr UINT WM_CAPTURE_PREVIEW = WM_APP + 100;
    inline constexpr UINT WM_CAPTURE_DONE = WM_APP + 101;

    /// The final combination; 0 when cleared with ESC.
    using DoneCallback = std::function<void(uint64_t mask)>;

    /// Before Input::Start.
    void Register();

    bool IsActive();
    /// What has been typed so far, for the window handling WM_CAPTURE_PREVIEW.
    uint64_t CapturedMask();

    bool Start(HWND target, DoneCallback done);
    /// WM_CAPTURE_DONE handler: the only place a running capture may be torn down.
    void Finish();
    /// A capture still running when its window goes away.
    void Cancel();
}
