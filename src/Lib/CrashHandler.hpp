#ifndef EASYMIC_CRASHHANDLER_HPP
#define EASYMIC_CRASHHANDLER_HPP

#include <string>
#include <functional>

namespace CrashHandler {
    using LogCallback = std::function<void(const std::string& info)>;

    struct Config {
        bool showErrorDialog = false;            // Show error message to user
        bool terminateOnException = true;        // Terminate process after handling
        LogCallback logCallback = nullptr;       // Custom logging function
    };

    /**
     * Initialize global exception handler
     * Must be called early in main/WinMain
     * @param config Handler configuration
     * @return true if initialized successfully
     */
    bool Initialize(const Config& config = Config());

    void Shutdown();

    /**
     * Get last exception information (if available)
     * Only valid immediately after an exception was caught
     */
    std::string GetLastExceptionInfo();

} // namespace CrashHandler

#endif // EASYMIC_CRASHHANDLER_HPP