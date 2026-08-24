#ifndef EASYMIC_INJECTION_HPP
#define EASYMIC_INJECTION_HPP

#include <windows.h>

/// Copies a position-independent function plus its parameter block into another process and runs it.
template<typename T>
inline bool InjectShellcode(DWORD pid, T params, PVOID pRemoteFunc, SIZE_T codeSize, bool freeMemory = true) {
    HANDLE hProcess = OpenProcess(
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ | PROCESS_CREATE_THREAD,
        FALSE, pid
    );
    if (!hProcess) {
        return false;
    }

    LPVOID pRemoteCode = VirtualAllocEx(hProcess, nullptr, codeSize,
                                        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    LPVOID pRemoteParams = VirtualAllocEx(hProcess, nullptr, sizeof(T),
                                          MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

    WriteProcessMemory(hProcess, pRemoteCode, pRemoteFunc, codeSize, nullptr);
    WriteProcessMemory(hProcess, pRemoteParams, &params, sizeof(T), nullptr);

    HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0,
                                        (LPTHREAD_START_ROUTINE) pRemoteCode, pRemoteParams, 0, nullptr);
    if (hThread) {
        CloseHandle(hThread);
    }

    if (freeMemory) {
        VirtualFreeEx(hProcess, pRemoteCode, 0, MEM_RELEASE);
        VirtualFreeEx(hProcess, pRemoteParams, 0, MEM_RELEASE);
    }

    CloseHandle(hProcess);
    return hThread != nullptr;
}

#endif //EASYMIC_INJECTION_HPP
