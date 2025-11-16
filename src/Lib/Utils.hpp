#ifndef EASYMIC_UTILS_HPP
#define EASYMIC_UTILS_HPP

#include <commctrl.h>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>
#include <windows.h>
#include <winver.h>
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

    // Get application version from version info resource
    static std::string GetApplicationVersion() {
        wchar_t modulePath[MAX_PATH];
        if (!GetModuleFileNameW(nullptr, modulePath, MAX_PATH)) {
            return "1.0.0.0";
        }

        DWORD verHandle = 0;
        DWORD verSize = GetFileVersionInfoSizeW(modulePath, &verHandle);
        if (verSize == 0) {
            return "1.0.0.0";
        }

        std::vector<BYTE> verData(verSize);
        if (!GetFileVersionInfoW(modulePath, verHandle, verSize, verData.data())) {
            return "1.0.0.0";
        }

        VS_FIXEDFILEINFO* pFileInfo = nullptr;
        UINT puLenFileInfo = 0;
        if (!VerQueryValueW(verData.data(), L"\\", (VOID**)&pFileInfo, &puLenFileInfo)) {
            return "1.0.0.0";
        }

        // Extract version numbers
        DWORD major = (pFileInfo->dwFileVersionMS >> 16) & 0xffff;
        DWORD minor = (pFileInfo->dwFileVersionMS) & 0xffff;
        DWORD patch = (pFileInfo->dwFileVersionLS >> 16) & 0xffff;
        DWORD build = (pFileInfo->dwFileVersionLS) & 0xffff;

        return std::to_string(major) + "." + std::to_string(minor) + "." + 
               std::to_string(patch) + "." + std::to_string(build);
    }
}

#endif //EASYMIC_UTILS_HPP
