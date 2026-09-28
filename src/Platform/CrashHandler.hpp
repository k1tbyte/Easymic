#pragma once

#include <string>
#include <functional>

namespace CrashHandler {
    using LogCallback = std::function<void(const std::string& info)>;

    /// Installs the handlers: a crash is reported to `log`, then the process ends. Early in WinMain.
    bool Initialize(LogCallback log);

} // namespace CrashHandler

