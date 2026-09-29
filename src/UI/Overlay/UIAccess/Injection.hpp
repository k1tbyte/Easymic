#pragma once

#include <windows.h>

/// Copies a position-independent function plus its parameter block into another process and runs it.
/// `returns`: the function ends, so its memory is freed once it has; a message loop keeps it for good.
template<typename T>
inline bool InjectShellcode(DWORD pid, const T& params, PVOID function, SIZE_T codeSize, bool returns) {
    const HANDLE process = OpenProcess(
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ | PROCESS_CREATE_THREAD,
        FALSE, pid
    );
    if (!process) {
        return false;
    }

    // One block: the code, its parameters right after it
    const auto code = static_cast<BYTE*>(VirtualAllocEx(process, nullptr, codeSize + sizeof(T),
                                                        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    const HANDLE thread = code
                          && WriteProcessMemory(process, code, function, codeSize, nullptr)
                          && WriteProcessMemory(process, code + codeSize, &params, sizeof(T), nullptr)
        ? CreateRemoteThread(process, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(code),
                             code + codeSize, 0, nullptr)
        : nullptr;

    // Freed only once nothing runs it: never started, or finished
    if (code && (!thread || (returns && WaitForSingleObject(thread, 1000) == WAIT_OBJECT_0))) {
        VirtualFreeEx(process, code, 0, MEM_RELEASE);
    }
    if (thread) {
        CloseHandle(thread);
    }
    CloseHandle(process);
    return thread != nullptr;
}
