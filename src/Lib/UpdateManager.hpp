#ifndef EASYMIC_UPDATEMANAGER_HPP
#define EASYMIC_UPDATEMANAGER_HPP

#include <string>
#include <functional>
#include <memory>
#include <vector>
#include "Version.hpp"

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
    UpdateManager();
    ~UpdateManager();

    // Check for updates asynchronously
    void CheckForUpdatesAsync(std::function<void(bool hasUpdate, const std::string& error)> callback);
    
    // Get information about the latest release
    bool HasPendingUpdate() const { return hasUpdate_; }
    const GitHubRelease& GetLatestRelease() const { return latestRelease_; }
    Version GetLatestVersion() const;
    
    // Show update notification to user
    void ShowUpdateNotification();
    
    // Download and apply update
    void DownloadAndInstallUpdate();

private:
    bool hasUpdate_ = false;
    GitHubRelease latestRelease_;
    
    // HTTP request helper
    std::string MakeHttpRequest(const std::string& url);
    static std::string GetApiUrl();
    std::string DownloadFile(const std::string& url, const std::string& filename);
    void ApplyUpdate(const std::string& filePath);
};

#endif //EASYMIC_UPDATEMANAGER_HPP

