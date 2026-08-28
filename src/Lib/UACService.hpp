#pragma once

/**
 * @brief Elevation checks and the scheduled task that skips the UAC prompt.
 *
 * The task runs this exact executable with the highest available privileges, so starting it is
 * the same as accepting the prompt once and having the answer remembered.
 */
namespace UAC {

    /// True when this process already has administrator privileges.
    bool IsElevated();

    /// Relaunches elevated and terminates this process. False when the user declined or cannot elevate.
    bool RequestElevation();

    /// True when the task exists and still points at this executable.
    bool IsSkipUACEnabled();

    /// Creates or removes the task. Both need elevation.
    bool EnableSkipUAC();
    bool DisableSkipUAC();

    /// Starts the elevated instance through the task. Needs no elevation, needs the task.
    bool RunWithSkipUAC();

} // namespace UAC

