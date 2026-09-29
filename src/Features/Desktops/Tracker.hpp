#pragma once

class Feedback;
struct DesktopSettings;

/// The shell writes the current desktop to the registry, so one waited-on event replaces polling.
namespace Tracker {

    /// Settings and feedback are only touched on the UI thread.
    void Start(const Feedback& feedback, const DesktopSettings& settings);
}
