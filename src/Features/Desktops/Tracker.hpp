#pragma once

class Feedback;
struct DesktopSettings;

/**
 * @brief Follows the current desktop: announces a switch on the overlay, feeds the tray icon.
 *
 * The shell writes the current desktop to the registry, so there is no COM poll and no message
 * hook: one waited-on event, and its callback resolves the name on the threadpool. The COM
 * facade is never touched on the UI thread - the tray reads what the callback last published.
 */
namespace Tracker {

    /// Arms the registry watch. Settings and feedback are only touched on the UI thread.
    void Start(const Feedback& feedback, const DesktopSettings& settings);
}
