#pragma once

namespace CrashHandler {
    /// A crash is reported to `log`, then the process ends. Early in WinMain.
    void Install(void (*log)(const char* report));
}
