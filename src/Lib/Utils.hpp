#ifndef EASYMIC_UTILS_HPP
#define EASYMIC_UTILS_HPP

#include <commctrl.h>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <windows.h>
#include "Resource.hpp"


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
                    TRUE,  // redraw flag
                    minMax);

        SendMessage(hWnd, TBM_SETPAGESIZE,
                    0, pageSize);                  // new page size

        SendMessage(hWnd, TBM_SETPOS, TRUE, value);
    }

    static bool IsCheckboxCheck(HWND hwndCheckbox, int id) {
        return SendMessage(GetDlgItem(hwndCheckbox, id), BM_GETCHECK, 0, 0) == BST_CHECKED;
    }

    static Resource LoadResource(HINSTANCE hInst, LPCSTR resourceName, LPCSTR resourceType)
    {
        HRSRC hResInfo = FindResource(hInst, resourceName, resourceType);
        if (!hResInfo) return {};

        HANDLE hRes = ::LoadResource(hInst, hResInfo);
        if (!hRes) return {};

        BYTE* buffer = (BYTE*)LockResource(hRes);
        DWORD size = SizeofResource(hInst, hResInfo);

        return {buffer, size};  // Embedded resource, doesn't own memory
    }

    static Resource LoadFileAsResource(const std::string& filePath)
    {
        std::ifstream file(filePath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            return {};
        }

        auto fileSize = file.tellg();
        if (fileSize <= 0) {
            return {};
        }

        file.seekg(0, std::ios::beg);

        auto buffer = std::make_unique<BYTE[]>(fileSize);
        if (!file.read(reinterpret_cast<char*>(buffer.get()), fileSize)) {
            return {};
        }

        return {std::move(buffer), static_cast<DWORD>(fileSize)};
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

    static bool DoesFileExist(const std::string& filePath) {
        DWORD attributes = GetFileAttributesA(filePath.c_str());
        return (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY));
    }

// Sound ComboBox management functions
    static void PopulateSourceComboBox(HWND comboBox, const std::set<std::string>& recentSources, const std::string& currentSource = "") {
        // Clear existing items
        SendMessage(comboBox, CB_RESETCONTENT, 0, 0);

        // Always add "Default" as first item
        SendMessage(comboBox, CB_ADDSTRING, 0, (LPARAM)"Default");

        int selectedIndex = 0;
        int itemIndex = 1;

        // Add recent sources that still exist
        for (const auto& source : recentSources) {
            if (DoesFileExist(source)) {
                const auto fileName = std::filesystem::path(source).filename().string();
                SendMessage(comboBox, CB_ADDSTRING, 0, (LPARAM)fileName.c_str());
                if (source == currentSource) {
                    selectedIndex = itemIndex;
                }
                itemIndex++;
            }
        }

        // Set selection
        SendMessage(comboBox, CB_SETCURSEL, selectedIndex, 0);
    }

    static void CleanupInvalidSources(std::set<std::string>& recentSources) {
        auto it = recentSources.begin();
        while (it != recentSources.end()) {
            if (!DoesFileExist(*it)) {
                it = recentSources.erase(it);
            } else {
                ++it;
            }
        }
    }

    static std::string GetSelectedSoundSource(HWND comboBox) {
        int selectedIndex = SendMessage(comboBox, CB_GETCURSEL, 0, 0);
        if (selectedIndex == 0 || selectedIndex == CB_ERR) {
            return "";  // Default selected or error
        }

        char buffer[MAX_PATH];
        int length = SendMessage(comboBox, CB_GETLBTEXT, selectedIndex, (LPARAM)buffer);
        if (length == CB_ERR) {
            return "";
        }

        return std::string(buffer, length);
    }

    static void AddToRecentSources(std::set<std::string>& recentSources, const std::string& source, size_t maxItems = 10) {
        if (!source.empty() && DoesFileExist(source)) {
            recentSources.insert(source);

            // Keep only recent N items (optional: implement LRU if needed)
            if (recentSources.size() > maxItems) {
                // For now, just keep all valid items
                // Could implement LRU policy later if needed
            }
        }
    }

    static std::string WideToUtf8(const std::wstring& wstr) {
        if (wstr.empty()) return {};

        int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (size <= 0) return {};

        std::string result(size - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], size, nullptr, nullptr);

        return result;
    }

    template<typename T>
    bool InjectShellcode(DWORD pid, T params, PVOID pRemoteFunc, SIZE_T codeSize, bool freeMemory = true) {
        HANDLE hProcess = OpenProcess(
            PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ | PROCESS_CREATE_THREAD,
            FALSE, pid
        );
        if (!hProcess) return FALSE;
        LPVOID pRemoteCode = VirtualAllocEx(
            hProcess,
            nullptr,
            codeSize,
            MEM_COMMIT | MEM_RESERVE,
            PAGE_EXECUTE_READWRITE
        );

        LPVOID pRemoteParams = VirtualAllocEx(
            hProcess,
            nullptr,
            sizeof(T),
            MEM_COMMIT | MEM_RESERVE,
            PAGE_READWRITE
        );

        WriteProcessMemory(hProcess, pRemoteCode, pRemoteFunc, codeSize, nullptr);
        WriteProcessMemory(hProcess, pRemoteParams, &params, sizeof(T), nullptr);

        HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0,
                                        (LPTHREAD_START_ROUTINE) pRemoteCode, pRemoteParams, 0, NULL);
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
}

#endif //EASYMIC_UTILS_HPP
