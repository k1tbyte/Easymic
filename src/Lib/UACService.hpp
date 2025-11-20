#ifndef EASYMIC_UACSERVICE_HPP
#define EASYMIC_UACSERVICE_HPP

#include <string>

namespace UAC {

    /**
     * User Account Control (UAC) utilities
     * Provides functions for elevation checking, process elevation, and UAC bypass
     */

    // Core UAC status checking

    /**
     * Check if current process is running with elevated privileges
     * @return true if process has administrator privileges
     */
    bool IsElevated();

    /**
     * Check if current user can elevate privileges
     * @return true if user has the ability to elevate (is administrator)
     */
    bool CanElevate();

    // Process elevation

    /**
     * Request elevation for current process
     * If successful, launches elevated instance and terminates current process
     * @return true if elevation request was initiated successfully
     */
    bool RequestElevation();

    // UAC bypass functionality (requires elevation to set up)

    /**
     * Check if UAC skip functionality is available
     * @return true if current process has elevation (required to manage tasks)
     */
    bool IsSkipUACAvailable();

    /**
     * Check if UAC skip is currently enabled for this application
     * @return true if scheduled task exists and is valid
     */
    bool IsSkipUACEnabled();

    /**
     * Enable UAC skip by creating a scheduled task with highest privileges
     * Requires elevated privileges
     * @return true if task was created successfully
     */
    bool EnableSkipUAC();

    /**
     * Disable UAC skip by removing the scheduled task
     * Requires elevated privileges
     * @return true if task was removed successfully
     */
    bool DisableSkipUAC();

    /**
     * Launch current application with elevated privileges via scheduled task
     * Does not require elevation to call, but requires task to be set up
     * @return true if elevated instance was launched successfully
     */
    bool RunWithSkipUAC();

} // namespace UAC

#endif // EASYMIC_UACSERVICE_HPP