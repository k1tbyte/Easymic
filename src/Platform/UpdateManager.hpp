#pragma once

#include <functional>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

#include "Version.hpp"

// Forward declaration to avoid circular includes
struct AppConfig;

struct GitHubAsset {
    std::string name;
    std::string browser_download_url;
};

struct GitHubRelease {
    std::string tag_name;
    std::string body;
    std::vector<GitHubAsset> assets;
};

class UpdateManager {
public:
    explicit UpdateManager(AppConfig& config);
    ~UpdateManager();

    /// The callback runs on the UI thread after the background check completes.
    void CheckForUpdatesAsync(std::function<void(bool hasUpdate, const std::string& error)> callback);
    void Stop();

    void ShowUpdateNotification();
    void SkipVersion();
    void DownloadAndInstallUpdate();

private:
    AppConfig& _cfg;
    bool _hasUpdate = false;
    GitHubRelease _latestRelease;
    std::thread _updateWorker;

    static std::vector<GitHubAsset> GetExecutableAssets(const GitHubRelease& release);
    bool IsVersionSkipped(const std::string& version) const;

    static INT_PTR CALLBACK UpdateDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

    static std::string GetApiUrl();
    /// @return the file it landed in, empty when the download failed.
    static std::wstring DownloadFile(const std::string& url, const std::string& filename);
    static bool ApplyUpdate(const std::wstring& filePath);
};

