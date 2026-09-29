#pragma once

#include <windows.h>

#include <functional>
#include <string>

namespace CommandRunner {

    /// Own thread: ShellExecute can sit on a UAC prompt for seconds. {dir} resolves against `context`, read at the key press.
    void Run(const std::string& command, HWND context);

    /// Output flattened to one line, killed after 10 s; onFinished lands on the runner thread.
    void RunCaptured(const std::string& command, HWND context, std::function<void(std::string)> onFinished);
}
