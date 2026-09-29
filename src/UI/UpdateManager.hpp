#pragma once

#include <string>
#include <thread>
#include <windows.h>

struct AppConfig;

class UpdateManager {
public:
    explicit UpdateManager(AppConfig& config);
    ~UpdateManager();

    void CheckForUpdatesAsync();

    /// Call once the single-instance mutex is released, or the new instance exits as a duplicate.
    void RelaunchIfInstalled() const;

    /// The renamed previous executable of an earlier install: it cannot be deleted while it runs.
    static void DeleteStaleExecutable();

private:
    struct Release {
        std::string Tag;
        std::string Notes;
        std::string AssetName;
        std::string AssetUrl;
    };

    void JoinWorker();
    void ShowUpdateNotification();
    void SkipVersion();
    void DownloadAndInstallUpdate();
    bool ApplyUpdate(const std::wstring& downloaded);

    static INT_PTR CALLBACK UpdateDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

    AppConfig& _cfg;
    Release _release;
    bool _installed = false;
    std::thread _worker;
};
