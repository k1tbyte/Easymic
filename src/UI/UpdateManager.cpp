#include "UpdateManager.hpp"

#include <algorithm>
#include <optional>
#include <vector>
#include <wininet.h>
#include <glaze/glaze.hpp>

#include "AppConfig.hpp"
#include "Core/Dispatcher.hpp"
#include "File.hpp"
#include "Resources/Resource.h"
#include "Settings/DialogControls.hpp"
#include "Str.hpp"
#include "System/Version.hpp"
#include "definitions.h"

#pragma comment(lib, "wininet.lib")

namespace {

    struct GitHubAsset {
        std::string name;
        std::string browser_download_url;
    };

    struct GitHubRelease {
        std::string tag_name;
        std::string body;
        std::vector<GitHubAsset> assets;
    };

    constexpr auto ApiUrl = "https://api.github.com/repos/" DEV_NAME "/" REPO_NAME "/releases/latest";

    class InternetHandle {
        HINTERNET _handle;
    public:
        explicit InternetHandle(HINTERNET handle) : _handle(handle) {}
        ~InternetHandle() { if (_handle) InternetCloseHandle(_handle); }

        InternetHandle(const InternetHandle&) = delete;
        InternetHandle& operator=(const InternetHandle&) = delete;

        operator HINTERNET() const { return _handle; }
        explicit operator bool() const { return _handle != nullptr; }
    };

    /// Status and read checks are the point: an error page or a cut download would otherwise be
    /// written to disk and installed over the running executable.
    std::optional<std::string> _fetch(const std::string& url) {
        constexpr DWORD NetworkTimeoutMs = 10000;
        const InternetHandle session(InternetOpenA("EasyLauncher-Updater", INTERNET_OPEN_TYPE_PRECONFIG,
                                                   nullptr, nullptr, 0));
        if (!session) {
            return std::nullopt;
        }

        DWORD timeout = NetworkTimeoutMs;
        InternetSetOptionA(session, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
        InternetSetOptionA(session, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

        const InternetHandle connection(InternetOpenUrlA(session, url.c_str(), nullptr, 0,
                                                         INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0));
        if (!connection) {
            return std::nullopt;
        }

        DWORD status = 0;
        DWORD statusSize = sizeof(status);
        if (!HttpQueryInfoA(connection, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                            &status, &statusSize, nullptr) || status != HTTP_STATUS_OK) {
            LOG_ERROR("Update request for '%s' answered %lu", url.c_str(), status);
            return std::nullopt;
        }

        std::string body;
        char buffer[8192];
        DWORD bytesRead = 0;
        while (InternetReadFile(connection, buffer, sizeof(buffer), &bytesRead)) {
            if (bytesRead == 0) {
                return body;
            }
            body.append(buffer, bytesRead);
        }
        return std::nullopt;
    }

    std::wstring _download(const std::string& url, const std::string& filename) {
        wchar_t tempPath[MAX_PATH];
        const DWORD tempLength = GetTempPathW(MAX_PATH, tempPath);
        if (tempLength == 0 || tempLength > MAX_PATH) {
            LOG_ERROR("Update download: no temp directory (0x%08lX)", GetLastError());
            return {};
        }

        // Widening byte by byte would turn a non-ASCII asset name into a path that does not exist
        const std::wstring downloadPath = std::wstring(tempPath, tempLength) + Str::Utf8ToWide(filename);

        const std::optional<std::string> body = _fetch(url);
        if (!body || !File::Write(downloadPath.c_str(), *body)) {
            LOG_ERROR("Update download failed");
            DeleteFileW(downloadPath.c_str());
            return {};
        }
        return downloadPath;
    }

    std::wstring _executablePath() {
        wchar_t path[MAX_PATH];
        const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
        return length == MAX_PATH ? std::wstring{} : std::wstring(path, length);
    }

    std::wstring _previousPath(const std::wstring& executable) {
        return executable + L".old";
    }
}

UpdateManager::UpdateManager(AppConfig& config) : _cfg(config) {
}

UpdateManager::~UpdateManager() {
    // Detached, not joined: a check stalled in WinINet must not hold up quitting. The worker
    // touches nothing of ours but a UI post, and that fails once the window is gone.
    if (_worker.joinable()) {
        _worker.detach();
    }
}

void UpdateManager::JoinWorker() {
    if (_worker.joinable()) {
        _worker.join();
    }
}

void UpdateManager::CheckForUpdatesAsync() {
    JoinWorker();
    _worker = std::thread([this] {
        const std::optional<std::string> response = _fetch(ApiUrl);
        GitHubRelease release;
        if (!response || glz::read<glz::opts{.error_on_unknown_keys = false}>(release, *response)) {
            return;
        }

        const auto asset = std::ranges::find_if(release.assets, [](const GitHubAsset& candidate) {
            return candidate.name.ends_with(".exe");
        });
        if (asset == release.assets.end() || !(Version(release.tag_name) > Version::App())) {
            return;
        }

        Dispatcher::ToUi([this, found = Release{release.tag_name, release.body, asset->name,
                                                asset->browser_download_url}]() mutable {
            if (_cfg.Core.SkippedVersions.contains(found.Tag)) {
                return;
            }
            _release = std::move(found);
            ShowUpdateNotification();
        });
    });
}

void UpdateManager::ShowUpdateNotification() {
    if (_cfg.Core.AutoUpdate) {
        DownloadAndInstallUpdate();
        return;
    }

    const INT_PTR result = DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_UPDATE_DIALOG),
                                           nullptr, UpdateDialogProc, reinterpret_cast<LPARAM>(this));
    if (result == IDC_UPDATE_INSTALL) {
        DownloadAndInstallUpdate();
    } else if (result == IDC_UPDATE_SKIP) {
        SkipVersion();
    }
}

void UpdateManager::DownloadAndInstallUpdate() {
    JoinWorker();
    _worker = std::thread([this, url = _release.AssetUrl, name = _release.AssetName] {
        const std::wstring downloaded = _download(url, name);
        Dispatcher::ToUi([this, downloaded] {
            if (downloaded.empty() || !ApplyUpdate(downloaded)) {
                MessageBoxW(nullptr, L"The update could not be downloaded or installed.", L"Update Error",
                            MB_ICONERROR);
            }
        });
    });
}

bool UpdateManager::ApplyUpdate(const std::wstring& downloaded) {
    const std::wstring executable = _executablePath();
    if (executable.empty()) {
        return false;
    }

    // A running image can be renamed but not overwritten, so the old one steps aside first
    const std::wstring previous = _previousPath(executable);
    DeleteFileW(previous.c_str());
    if (!MoveFileExW(executable.c_str(), previous.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        LOG_ERROR("Update: cannot move the running executable aside (0x%08lX)", GetLastError());
        DeleteFileW(downloaded.c_str());
        return false;
    }

    if (!MoveFileExW(downloaded.c_str(), executable.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING)) {
        LOG_ERROR("Update: cannot move the download into place (0x%08lX)", GetLastError());
        MoveFileExW(previous.c_str(), executable.c_str(), MOVEFILE_REPLACE_EXISTING);
        DeleteFileW(downloaded.c_str());
        return false;
    }

    _installed = true;
    PostQuitMessage(0);
    return true;
}

void UpdateManager::RelaunchIfInstalled() const {
    if (!_installed) {
        return;
    }

    std::wstring command = L"\"" + _executablePath() + L"\"";
    STARTUPINFOW startup{.cb = sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
        LOG_ERROR("Update: relaunch failed (0x%08lX)", GetLastError());
        return;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
}

void UpdateManager::DeleteStaleExecutable() {
    if (const std::wstring executable = _executablePath(); !executable.empty()) {
        DeleteFileW(_previousPath(executable).c_str());
    }
}

void UpdateManager::SkipVersion() {
    // Only this field goes to disk: the live config may hold the settings window's unsaved edits
    _cfg.Core.SkippedVersions.insert(_release.Tag);
    AppConfig stored = AppConfig::Load();
    stored.Core.SkippedVersions.insert(_release.Tag);
    stored.Save();
}

INT_PTR CALLBACK UpdateManager::UpdateDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_INITDIALOG: {
            const Release& release = reinterpret_cast<const UpdateManager*>(lParam)->_release;
            SetDlgItemTextW(hDlg, IDC_UPDATE_VERSION, Str::Utf8ToWide(release.Tag).c_str());
            SetDlgItemTextW(hDlg, IDC_UPDATE_NOTES,
                            Str::Utf8ToWide(release.Notes.empty() ? "No release notes available." : release.Notes).c_str());
            DialogControls::CenterOnScreen(hDlg);
            return TRUE;
        }

        case WM_CLOSE:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;

        case WM_COMMAND: {
            const UINT id = LOWORD(wParam);
            if (id == IDC_UPDATE_INSTALL || id == IDC_UPDATE_SKIP || id == IDC_UPDATE_LATER || id == IDCANCEL) {
                EndDialog(hDlg, id);
                return TRUE;
            }
            return FALSE;
        }

        default:
            return FALSE;
    }
}
