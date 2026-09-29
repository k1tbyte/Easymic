#include "UIAccess.hpp"

#include <vector>
#include <tlhelp32.h>

#include "definitions.h"
#include "Injection.hpp"

namespace {

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

/// MSVC gives no way to measure a function, so the copy takes more than either thread function needs
constexpr SIZE_T ShellcodeSize = 2048;


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

VOID WINAPI AffinityRemoteThreadFunc(LPVOID lpParam) {
    ShellcodeAffinityParams* params = (ShellcodeAffinityParams*)lpParam;

    typedef BOOL (WINAPI *SetWindowDisplayAffinityFunc)(HWND, DWORD);
    SetWindowDisplayAffinityFunc fnSetWindowDisplayAffinity =
        (SetWindowDisplayAffinityFunc)params->pfSetWindowDisplayAffinity;

    fnSetWindowDisplayAffinity(params->hWnd, params->affinity);
}

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

    return InjectShellcode(pid, params, (PVOID)WindowCreateRemoteThreadFunc, ShellcodeSize, false);
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

} // anonymous namespace

HWND UIAccess::GetOrCreateWindow(const char *key, DWORD exStyle, DWORD style) {
    auto uiAccessPids = FindUiAccessProcesses();
    if (uiAccessPids.empty()) {
        LOG_ERROR("No UIAccess processes found");
        return nullptr;
    }

    auto hwnd = GetWindowsByTitle(uiAccessPids, key);

    if (!hwnd) {
        if (!InjectToProcess(uiAccessPids[0], key, exStyle, style)) {
            LOG_ERROR("Failed to inject UIAccess process for window creation");
            return nullptr;
        }

        // Wait a bit for the window to be created
        Sleep(100);
    }

    return GetWindowsByTitle(uiAccessPids, key);
}

bool UIAccess::InjectDisplayAffinity(HWND hWnd, DWORD affinity) {
    DWORD pid;
    GetWindowThreadProcessId(hWnd, &pid);
    if (pid == 0) {
            LOG_INFO("[InjectDisplayAffinity] Failed to get PID for HWND %p", hWnd);
        return false;
    }
    HMODULE hUser32 = GetModuleHandleA("user32.dll");
    ShellcodeAffinityParams params = {};
    params.pfSetWindowDisplayAffinity = GetProcAddress(hUser32, "SetWindowDisplayAffinity");
    params.hWnd = hWnd;
    params.affinity = affinity;
    const auto result = InjectShellcode(pid, params, (PVOID)AffinityRemoteThreadFunc, ShellcodeSize, true);
    LOG_INFO("[InjectDisplayAffinity] Injected display affinity (%d) into process %d: %s",
             affinity, pid, result ? "Success" : "Failure");

    return result;
}
