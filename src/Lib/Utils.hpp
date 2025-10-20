#ifndef EASYMIC_UTILS_HPP
#define EASYMIC_UTILS_HPP

#include <commctrl.h>
#include <tlhelp32.h>

struct Resource {
    BYTE* buffer;
    DWORD fileSize;
};

namespace Utils {

    #define AutoStartupHKEY LR"(SOFTWARE\Microsoft\Windows\CurrentVersion\Run)"

    static void CenterWindowOnScreen(HWND hWnd)
    {
        HWND hWndDesktop = GetDesktopWindow();
        RECT rcDlg, rcOwner, rc;

        GetWindowRect(hWndDesktop, &rcOwner);
        GetWindowRect(hWnd, &rcDlg);
        CopyRect(&rc, &rcOwner);

        OffsetRect(&rcDlg, -rcDlg.left, -rcDlg.top);
        OffsetRect(&rc, -rc.left, -rc.top);
        OffsetRect(&rc, -rcDlg.right, -rcDlg.bottom);

        SetWindowPos(hWnd, HWND_TOP,
                     rcOwner.left + (rc.right / 2),
                     rcOwner.top + (rc.bottom / 2), 0, 0, SWP_NOSIZE
        );
    }

    static void InitTrackbar(HWND hWnd, LPARAM pageSize, LPARAM minMax, LPARAM value)
    {
        SendMessage(hWnd, TBM_SETRANGE,
                    (WPARAM) TRUE,  // redraw flag
                    minMax);

        SendMessage(hWnd, TBM_SETPAGESIZE,
                    0, pageSize);                  // new page size

        SendMessage(hWnd, TBM_SETPOS, (WPARAM) TRUE, value);
    }

    static bool IsCheckboxCheck(HWND hwndCheckbox, int id) {
        return SendMessage(GetDlgItem(hwndCheckbox, id), BM_GETCHECK, 0, 0) == BST_CHECKED;
    }

    static void LoadResource(HINSTANCE hInst,LPCSTR resourceName, LPCSTR resourceType, BYTE** buffer, DWORD* size)
    {
        HRSRC hResInfo = FindResource(hInst,resourceName, resourceType);
        HANDLE hRes = LoadResource(hInst, hResInfo);
        *buffer = (BYTE*)LockResource(hRes);
        *size = SizeofResource(hInst, hResInfo);
    }

    static bool IsUiAccessProcess(HANDLE hProcess) {
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

    static std::vector<DWORD> FindUiAccessProcesses() {
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

    static HWND GetUiAccessWindow() {

        struct CallbackData {
            HWND* outHwnd;
            DWORD targetPid;
        };

        auto processIds = FindUiAccessProcesses();
        HWND hWnd = nullptr;
        CallbackData data = { &hWnd, 0 };
        for (const auto& processId : processIds) {
            data.targetPid = processId;
            EnumWindows([](HWND hWnd, LPARAM lParam) {
                auto* pData = reinterpret_cast<CallbackData*>(lParam);
                DWORD windowPid;
                GetWindowThreadProcessId(hWnd, &windowPid);
                if (pData->targetPid != windowPid) {
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

    //#region <== Registry ==>

    static bool IsInAutoStartup(LPCWSTR appName)
    {
        return RegGetValueW(HKEY_CURRENT_USER,AutoStartupHKEY,
                            appName,RRF_RT_REG_SZ,
                            nullptr,nullptr,nullptr) == ERROR_SUCCESS;
    }

    static bool AddToAutoStartup(LPCWSTR appName)
    {
        HKEY hkey;
        auto result = RegOpenKeyExW(HKEY_CURRENT_USER,AutoStartupHKEY,0,
                                    KEY_WRITE,&hkey);

        if(result != ERROR_SUCCESS)
            return false;

        wchar_t szFileName[MAX_PATH];

        GetModuleFileNameW(nullptr, szFileName, MAX_PATH);
        return RegSetValueExW(hkey, appName, 0, REG_SZ, reinterpret_cast<const BYTE *>(szFileName),
                              wcslen(szFileName) * sizeof(wchar_t)) == ERROR_SUCCESS;
    }

    static bool RemoveFromAutoStartup(LPCWSTR appName)
    {
        return RegDeleteKeyValueW(HKEY_CURRENT_USER,AutoStartupHKEY,appName);
    }

//#endregion
}

#endif //EASYMIC_UTILS_HPP
