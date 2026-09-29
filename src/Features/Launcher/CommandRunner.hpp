#pragma once

#include <windows.h>

#include <functional>
#include <string>

/**
 * @brief Runs a user command line through the shell.
 *
 * Shell semantics on purpose: executables, shortcuts, folders, documents and URLs all work,
 * and a bare name is resolved through PATH. Shell builtins need an explicit "cmd /c ...".
 *
 * A command line may carry {dir}. It is resolved against the window handed in, so the caller
 * reads the foreground at the moment the action fires - by the time the thread below starts,
 * whatever we launch may already have taken it.
 */
namespace CommandRunner {

    inline constexpr char DirToken[] = "{dir}";

    /**
     * @brief Launches the command without blocking the caller.
     *
     * Always on a fresh thread: the hotkey worker queue is serial, and ShellExecuteEx can sit
     * for seconds on a UAC prompt or a slow shell handler - that would stall mic actions.
     */
    void Run(const std::string& command, HWND context);

    /**
     * @brief Runs the command and hands its output over once it has finished.
     *
     * The callback lands on this thread, not the caller's, and may be minutes late - it must not
     * touch anything that can be gone by then.
     */
    void RunCaptured(const std::string& command, HWND context, std::function<void(std::string)> onFinished);
}
