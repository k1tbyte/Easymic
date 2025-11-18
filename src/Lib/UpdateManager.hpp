#ifndef EASYMIC_UPDATEMANAGER_HPP
#define EASYMIC_UPDATEMANAGER_HPP

#include <string>
#include <functional>
#include <memory>
#include <vector>
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
    UpdateManager(AppConfig& config);
    ~UpdateManager();

    // Check for updates asynchronously
    void CheckForUpdatesAsync(std::function<void(bool hasUpdate, const std::string& error)> callback);
    
    // Get information about the latest release
    bool HasPendingUpdate() const { return hasUpdate_; }
    const GitHubRelease& GetLatestRelease() const { return latestRelease_; }
    Version GetLatestVersion() const;
    
    void ShowUpdateNotification();
    
    void SkipVersion();
    
    void DownloadAndInstallUpdate();

    std::vector<GitHubAsset> GetExecutableAssets() const;

private:
    AppConfig& config_;
    bool hasUpdate_ = false;
    GitHubRelease latestRelease_;
    
    bool IsVersionSkipped(const std::string& version) const;
    
    // Dialog procedure for custom update dialog
    static INT_PTR CALLBACK UpdateDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
    
    // HTTP request helper
    std::string MakeHttpRequest(const std::string& url);
    std::string GetApiUrl() const;
    std::string DownloadFile(const std::string& url, const std::string& filename);
    void ApplyUpdate(const std::string& filePath);
};

#endif //EASYMIC_UPDATEMANAGER_HPP

