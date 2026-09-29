#pragma once

#include <windows.h>

/// Runs a position-independent function with its parameters in another process; `returns` frees the copy once it ends.
template<typename T>
inline bool InjectShellcode(DWORD pid, const T& params, PVOID function, SIZE_T codeSize, bool returns) {
    const HANDLE process = OpenProcess(
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ | PROCESS_CREATE_THREAD,
        FALSE, pid
    );
    if (!process) {
        return false;
    }

    const auto code = static_cast<BYTE*>(VirtualAllocEx(process, nullptr, codeSize + sizeof(T),
                                                        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    const HANDLE thread = code
                          && WriteProcessMemory(process, code, function, codeSize, nullptr)
                          && WriteProcessMemory(process, code + codeSize, &params, sizeof(T), nullptr)
        ? CreateRemoteThread(process, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(code),
                             code + codeSize, 0, nullptr)
        : nullptr;

    if (code && (!thread || (returns && WaitForSingleObject(thread, 1000) == WAIT_OBJECT_0))) {
        VirtualFreeEx(process, code, 0, MEM_RELEASE);
    }
    if (thread) {
        CloseHandle(thread);
    }
    CloseHandle(process);
    return thread != nullptr;
}
