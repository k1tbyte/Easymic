//
// Created by kitbyte on 03.11.2025.
//

#include <vector>
#include "UIAccessManager.hpp"

#include <tlhelp32.h>

#include "Utils.hpp"


struct ShellcodeWindowCreateParams {
    FARPROC pfCreateWindowExA;
    FARPROC pfGetMessageA;
    FARPROC pfTranslateMessage;
    FARPROC pfDispatchMessageA;
    DWORD exStyle;
    DWORD style;

    char className[64];
    char windowTitle[64];
};

struct ShellcodeAffinityParams {
    FARPROC pfSetWindowDisplayAffinity;
    HWND hWnd;
    DWORD affinity;
};


/*extern "C" VOID WINAPI RemoteThreadFunc(LPVOID lpParam);
extern "C" SIZE_T RemoteThreadFuncSize;*/


VOID WINAPI WindowCreateRemoteThreadFunc(LPVOID lpParam) {
    const ShellcodeWindowCreateParams *params = (ShellcodeWindowCreateParams *) lpParam;

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


#ifdef __MINGW32__ // Shit working with MinGW but no with MSVC
DWORD WINAPI WindowCreateRemoteThreadFuncEnd() {
    return 0;
}
#endif

VOID WINAPI AffinityRemoteThreadFunc(LPVOID lpParam) {
    ShellcodeAffinityParams* params = (ShellcodeAffinityParams*)lpParam;

    typedef BOOL (WINAPI *SetWindowDisplayAffinityFunc)(HWND, DWORD);
    SetWindowDisplayAffinityFunc fnSetWindowDisplayAffinity =
        (SetWindowDisplayAffinityFunc)params->pfSetWindowDisplayAffinity;

    fnSetWindowDisplayAffinity(params->hWnd, params->affinity);
}

#ifdef __MINGW32__
DWORD WINAPI AffinityRemoteThreadFuncEnd() {
    return 0;
}
#endif

BOOL InjectToProcess(DWORD pid, LPCSTR title, DWORD exStyle, DWORD style) {
    HMODULE hUser32 = GetModuleHandleA("user32.dll");

    ShellcodeWindowCreateParams params = {};
    params.pfCreateWindowExA = GetProcAddress(hUser32, "CreateWindowExA");
    params.pfGetMessageA = GetProcAddress(hUser32, "GetMessageA");
    params.pfTranslateMessage = GetProcAddress(hUser32, "TranslateMessage");
    params.pfDispatchMessageA = GetProcAddress(hUser32, "DispatchMessageA");
    params.style = style;
    params.exStyle = exStyle;

    strcpy_s(params.className, "Static");
    strcpy_s(params.windowTitle, title);

#if _MSC_VER
    SIZE_T codeSize = 2048;
#else
    SIZE_T codeSize = (SIZE_T) WindowCreateRemoteThreadFuncEnd - (SIZE_T) WindowCreateRemoteThreadFunc;
#endif
    return Utils::InjectShellcode(pid, params, (PVOID)WindowCreateRemoteThreadFunc, codeSize, false);
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
        HWND *outHwnd;
        DWORD targetPid;
        LPCSTR targetTitle;
    };

    HWND hWnd{};
    CallbackData data = {&hWnd, 0, title};
    for (const auto &processId: pids) {
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

bool UIAccessManager::InjectDisplayAffinity(HWND hWnd, DWORD affinity) {
    DWORD pid;
    GetWindowThreadProcessId(hWnd, &pid);
    if (pid == 0) {
        return false;
    }
    HMODULE hUser32 = GetModuleHandleA("user32.dll");
    ShellcodeAffinityParams params = {};
    params.pfSetWindowDisplayAffinity = GetProcAddress(hUser32, "SetWindowDisplayAffinity");
    params.hWnd = hWnd;
    params.affinity = affinity;
#ifdef _MSC_VER
    SIZE_T codeSize = 2048;
#else
    SIZE_T codeSize = (SIZE_T) AffinityRemoteThreadFuncEnd - (SIZE_T) AffinityRemoteThreadFunc;
#endif
    return Utils::InjectShellcode(pid, params, (PVOID)AffinityRemoteThreadFunc, codeSize, false);
}
