//
// Created by kitbyte on 03.11.2025.
//

#include <vector>
#include "UIAccessManager.hpp"

#include <tlhelp32.h>

extern "C" VOID WINAPI RemoteThreadFuncAsm(LPVOID lpParam);
extern "C" SIZE_T RemoteThreadFuncAsmSize;

struct ShellcodeParams {
    FARPROC pfCreateWindowExA;
    FARPROC pfGetMessageA;
    FARPROC pfTranslateMessage;
    FARPROC pfDispatchMessageA;
    DWORD exStyle;
    DWORD style;

    char className[64];
    char windowTitle[64];
};

#pragma region Shit working with MinGW but no with MSVC
//
/*VOID WINAPI RemoteThreadFunc(LPVOID lpParam) {
    const ShellcodeParams *params = (ShellcodeParams *) lpParam;

    typedef HWND (WINAPI *CreateWindowExA_t)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE,
                                             LPVOID);
    typedef BOOL (WINAPI *GetMessageA_t)(LPMSG, HWND, UINT, UINT);
    typedef BOOL (WINAPI *TranslateMessage_t)(const MSG *);
    typedef LRESULT (WINAPI *DispatchMessageA_t)(const MSG *);

    auto fnCreateWindowExA = (CreateWindowExA_t) params->pfCreateWindowExA;
    auto fnGetMessageA = (GetMessageA_t) params->pfGetMessageA;
    auto fnTranslateMessage = (TranslateMessage_t) params->pfTranslateMessage;
    auto fnDispatchMessageA = (DispatchMessageA_t) params->pfDispatchMessageA;

    HWND hwnd = fnCreateWindowExA(
        params->exStyle,
        params->className,
        params->windowTitle,
        params->style,
        CW_USEDEFAULT, CW_USEDEFAULT, 0, 0,
        NULL, NULL, NULL, NULL
    );

    if (!hwnd) {
        return;
    }

    // Message loop
    MSG msg;
    while (fnGetMessageA(&msg, NULL, 0, 0) > 0) {
        fnTranslateMessage(&msg);
        fnDispatchMessageA(&msg);
    }

}

DWORD WINAPI RemoteThreadFuncEnd() {
    return 0;
}*/
#pragma endregion

BOOL InjectToProcess(DWORD pid, LPCSTR title, DWORD exStyle, DWORD style) {
    HANDLE hProcess = OpenProcess(
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ | PROCESS_CREATE_THREAD,
        FALSE, pid
    );
    if (!hProcess) return FALSE;

    HMODULE hUser32 = GetModuleHandleA("user32.dll");

    ShellcodeParams params = {};
    params.pfCreateWindowExA = GetProcAddress(hUser32, "CreateWindowExA");
    params.pfGetMessageA = GetProcAddress(hUser32, "GetMessageA");
    params.pfTranslateMessage = GetProcAddress(hUser32, "TranslateMessage");
    params.pfDispatchMessageA = GetProcAddress(hUser32, "DispatchMessageA");
    params.style = style;
    params.exStyle = exStyle;

    strcpy_s(params.className, "Static");
    strcpy_s(params.windowTitle, title);

    SIZE_T codeSize = RemoteThreadFuncAsmSize;
    LPVOID pRemoteCode = VirtualAllocEx(hProcess, NULL, codeSize,
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

    LPVOID pRemoteParams = VirtualAllocEx(hProcess, NULL,
        sizeof(ShellcodeParams), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

    WriteProcessMemory(hProcess, pRemoteCode, (LPVOID)RemoteThreadFuncAsm, codeSize, NULL);
    WriteProcessMemory(hProcess, pRemoteParams, &params, sizeof(params), NULL);

    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0,
        (LPTHREAD_START_ROUTINE)pRemoteCode, pRemoteParams, 0, NULL);

    if (hThread) {
        // We are not waiting for the thread to finish! The window should work independently
        CloseHandle(hThread);
    }

    // DO NOT free memory while the window is running!
    CloseHandle(hProcess);

    return (hThread != NULL);
}

bool IsUiAccessProcess(HANDLE hProcess) {
    HANDLE hToken;
    DWORD length;
    BOOL uiAccess = FALSE;

    if (OpenProcessToken(hProcess, TOKEN_QUERY, &hToken)) {
        if (GetTokenInformation(hToken, TokenUIAccess, &uiAccess, sizeof(uiAccess), &length)) {
            CloseHandle(hToken);
            return uiAccess == TRUE;
        }
        CloseHandle(hToken);
    }
    return false;
}

std::vector<DWORD> FindUiAccessProcesses() {
    std::vector<DWORD> processIds;
    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(PROCESSENTRY32W);

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return processIds;
    }

    if (Process32FirstW(snapshot, &entry)) {
        do {
            auto processHandle = OpenProcess(
                PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                FALSE,
                entry.th32ProcessID
            );

            if (!processHandle) {
                continue;
            }
            if (IsUiAccessProcess(processHandle)) {
                processIds.push_back(entry.th32ProcessID);
            }
            CloseHandle(processHandle);
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return processIds;
}

HWND GetWindowsByTitle(std::vector<DWORD> pids, LPCSTR title) {
    struct CallbackData {
        HWND* outHwnd;
        DWORD targetPid;
        LPCSTR targetTitle;
    };

    HWND hWnd{};
    CallbackData data = {&hWnd, 0, title};
    for (const auto &processId : pids) {
        data.targetPid = processId;
        EnumWindows([](HWND hWnd, LPARAM lParam) {
            auto *pData = reinterpret_cast<CallbackData *>(lParam);
            DWORD windowPid;
            GetWindowThreadProcessId(hWnd, &windowPid);
            if (pData->targetPid != windowPid) {
                return TRUE;
            }
            CHAR windowTitle[255];
            GetWindowTextA(hWnd, windowTitle, sizeof(windowTitle));
            if (strcmp(windowTitle, pData->targetTitle) != 0) {
                return TRUE;
            }

            *pData->outHwnd = hWnd;
            return FALSE;
        }, reinterpret_cast<LPARAM>(&data));

        if (hWnd) {
            break;
        }
    }

    return hWnd;
}

HWND UIAccessManager::GetOrCreateWindow(const char *key, DWORD exStyle, DWORD style) {
    auto uiAccessPids = FindUiAccessProcesses();
    if (uiAccessPids.empty()) {
        return nullptr;
    }

    auto hwnd = GetWindowsByTitle(uiAccessPids, key);

    if (!hwnd) {
        if (!InjectToProcess(uiAccessPids[0], key, exStyle, style)) {
            return nullptr;
        }

        // Wait a bit for the window to be created
        Sleep(100);

        hwnd = GetWindowsByTitle(uiAccessPids, key);
    }

    return hwnd;
}
