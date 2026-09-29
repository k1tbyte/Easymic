#pragma once

#include <windows.h>

struct KeyboardSettings;

/// The layout the user is typing in, as a pill on the overlay. UI thread only; WinEvents re-read it, nothing polls.
namespace LayoutLayer {

    void Register(KeyboardSettings& settings);

    /// A switch this app asked for, shown before the window takes it. Any thread.
    void Requested(HKL layout);
}
