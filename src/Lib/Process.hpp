#ifndef EASYMIC_PROCESS_HPP
#define EASYMIC_PROCESS_HPP

#include <string>
#include <windows.h>

namespace Process {

    /// Full image path of the process owning the window, empty when it cannot be queried.
    inline std::wstring GetNameByHWND(HWND hwnd) {
        if (!hwnd || !IsWindow(hwnd)) {
            return {};
        }

        DWORD processId = 0;
        GetWindowThreadProcessId(hwnd, &processId);
        if (processId == 0) {
            return {};
        }

        HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
        if (!hProcess) {
            return {};
        }

        wchar_t processName[MAX_PATH] = {};
        DWORD size = MAX_PATH;
        const bool ok = QueryFullProcessImageNameW(hProcess, 0, processName, &size);
        CloseHandle(hProcess);

        return ok ? std::wstring(processName, size) : std::wstring{};
    }
}

#endif //EASYMIC_PROCESS_HPP
