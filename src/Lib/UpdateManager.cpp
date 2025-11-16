#include "UpdateManager.hpp"
#include <windows.h>
#include <wininet.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>
#include <glaze/glaze.hpp>

#include "definitions.h"

#pragma comment(lib, "wininet.lib")

UpdateManager::UpdateManager() {
}

UpdateManager::~UpdateManager() {
}

std::string UpdateManager::GetApiUrl() {
    return "https://api.github.com/repos/" GITHUB_OWNER "/" GITHUB_REPO "/releases/latest";
}

void UpdateManager::CheckForUpdatesAsync(std::function<void(bool, const std::string&)> callback) {
    // Run in background thread to avoid blocking UI
    std::thread([this, callback]() {
        try {
            std::string response = MakeHttpRequest(GetApiUrl());
            if (response.empty()) {
                callback(false, "Failed to connect to GitHub");
                return;
            }
            
            // Use glaze to parse JSON response
            auto parseResult = glz::read<glz::opts{.error_on_unknown_keys = false}>(latestRelease_, response);
            if (parseResult) {
                callback(false, "Failed to parse JSON response: " + std::string(glz::format_error(parseResult, response)));
                return;
            }
            
            // Compare versions
            Version latestVersion(latestRelease_.tag_name);
            Version currentVersion = g_AppVersion;
            
            hasUpdate_ = latestVersion > currentVersion;
            callback(hasUpdate_, "");
            
        } catch (const std::exception& e) {
            callback(false, e.what());
        }
    }).detach();
}

std::string UpdateManager::MakeHttpRequest(const std::string& url) {
    HINTERNET hInternet = InternetOpenA("Easymic-Updater", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!hInternet) {
        return "";
    }

    HINTERNET hConnect = InternetOpenUrlA(hInternet, url.c_str(), nullptr, 0, 
                                         INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hConnect) {
        InternetCloseHandle(hInternet);
        return "";
    }

    std::string result;
    char buffer[4096];
    DWORD bytesRead;
    
    while (InternetReadFile(hConnect, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        result.append(buffer, bytesRead);
    }

    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);
    
    return result;
}

Version UpdateManager::GetLatestVersion() const {
    return Version(latestRelease_.tag_name);
}

void UpdateManager::ShowUpdateNotification() {
    if (!hasUpdate_) {
        return;
    }
    
    std::string message = "A new version (" + latestRelease_.tag_name + ") is available!\n\n";
    if (!latestRelease_.body.empty()) {
        message += "Release Notes:\n" + latestRelease_.body + "\n\n";
    }
    message += "Would you like to download and install the update?";
    
    int result = MessageBoxA(nullptr, message.c_str(), "Easymic | Update Available", MB_YESNO | MB_ICONINFORMATION);
    
    if (result == IDYES) {
        DownloadAndInstallUpdate();
    }
}

void UpdateManager::DownloadAndInstallUpdate() {
    if (!hasUpdate_ || latestRelease_.assets.empty()) {
        MessageBoxA(nullptr, "No update available or no assets found.", "Update Error", MB_ICONERROR);
        return;
    }
    
    // Find .exe asset
    GitHubAsset* exeAsset = nullptr;
    for (auto& asset : latestRelease_.assets) {
        if (asset.name.ends_with(".exe")) {
            exeAsset = &asset;
            break;
        }
    }
    
    if (!exeAsset) {
        MessageBoxA(nullptr, "No executable found in release assets.", "Update Error", MB_ICONERROR);
        return;
    }
    
    try {
        std::string downloadPath = DownloadFile(exeAsset->browser_download_url, exeAsset->name);
        ApplyUpdate(downloadPath);
    } catch (const std::exception& e) {
        std::string error = "Update failed: " + std::string(e.what());
        MessageBoxA(nullptr, error.c_str(), "Update Error", MB_ICONERROR);
    }
}

std::string UpdateManager::DownloadFile(const std::string& url, const std::string& filename) {
    // Get temp directory
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    std::string downloadPath = std::string(tempPath) + filename;
    
    HINTERNET hInternet = InternetOpenA("Easymic-Updater", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!hInternet) {
        throw std::runtime_error("Failed to initialize internet connection");
    }

    HINTERNET hConnect = InternetOpenUrlA(hInternet, url.c_str(), nullptr, 0, 
                                         INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hConnect) {
        InternetCloseHandle(hInternet);
        throw std::runtime_error("Failed to connect to download URL");
    }

    std::ofstream file(downloadPath, std::ios::binary);
    if (!file.is_open()) {
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        throw std::runtime_error("Failed to create download file");
    }

    char buffer[8192];
    DWORD bytesRead;
    
    while (InternetReadFile(hConnect, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        file.write(buffer, bytesRead);
    }

    file.close();
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);
    
    return downloadPath;
}

void UpdateManager::ApplyUpdate(const std::string& filePath) {
    wchar_t currentExecutable[MAX_PATH];
    GetModuleFileNameW(nullptr, currentExecutable, MAX_PATH);
    
    std::wstring wFilePath(filePath.begin(), filePath.end());
    
    // Create PowerShell command for update
    std::wstring psCommand = L"Start-Sleep -Seconds 2; ";
    psCommand += L"Copy-Item -Path '" + wFilePath + L"' -Destination '" + std::wstring(currentExecutable) + L"' -Force; ";
    psCommand += L"Remove-Item -Path '" + wFilePath + L"' -Force; ";
    psCommand += L"Start-Sleep -Seconds 1; ";
    psCommand += L"Start-Process -FilePath '" + std::wstring(currentExecutable) + L"';";
    
    std::wstring arguments = L"-WindowStyle Hidden -ExecutionPolicy Bypass -Command \"" + psCommand + L"\"";
    
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"open";
    sei.lpFile = L"powershell.exe";
    sei.lpParameters = arguments.c_str();
    sei.nShow = SW_HIDE;
    
    if (!ShellExecuteExW(&sei)) {
        throw std::runtime_error("Failed to start update process");
    }
    
    // Exit current application after a short delay
    std::thread([]() {
        Sleep(500);
        ExitProcess(0);
    }).detach();
}
