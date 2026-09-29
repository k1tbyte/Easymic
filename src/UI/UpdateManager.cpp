#include "UpdateManager.hpp"

#include <optional>
#include <thread>
#include <windows.h>
#include <wininet.h>
#include <glaze/glaze.hpp>

#include "AppConfig.hpp"
#include "Core/Dispatcher.hpp"
#include "File.hpp"
#include "Resources/Resource.h"
#include "Settings/DialogControls.hpp"
#include "Str.hpp"
#include "definitions.h"

#pragma comment(lib, "wininet.lib")

namespace {

    /// Closes a WinINet handle the way every other resource in this file is closed.
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

    /**
     * @brief The body of a URL, or nothing.
     *
     * The status and read checks are the point: an error page or a cut download would otherwise be
     * written to disk and then copied over the running executable.
     */
    std::optional<std::string> Fetch(const std::string& url) {
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

    /// Single quotes are the only PowerShell quoting that takes a string verbatim; the one
    /// character that still needs escaping inside them is the quote itself, doubled.
    std::wstring PsQuote(const std::wstring& text) {
        std::wstring quoted = L"'";
        for (const wchar_t character : text) {
            if (character == L'\'') {
                quoted += L'\'';
            }
            quoted += character;
        }
        return quoted + L"'";
    }
}

UpdateManager::UpdateManager(AppConfig& config) : _cfg(config) {
}

UpdateManager::~UpdateManager() {
    Stop();
}

void UpdateManager::Stop() {
    if (_updateWorker.joinable()) {
        _updateWorker.join();
    }
}

std::string UpdateManager::GetApiUrl() {
    return "https://api.github.com/repos/" GITHUB_OWNER "/" GITHUB_REPO "/releases/latest";
}

void UpdateManager::CheckForUpdatesAsync(std::function<void(bool, const std::string&)> callback) {
    Stop();
    _updateWorker = std::thread([this, callback = std::move(callback)]() mutable {
        GitHubRelease release;
        std::string error;
        bool hasUpdate = false;
        const std::optional<std::string> response = Fetch(GetApiUrl());

        if (!response) {
            error = "Failed to reach GitHub";
        } else if (const auto parseResult = glz::read<glz::opts{.error_on_unknown_keys = false}>(
                       release, *response)) {
            error = "Failed to parse JSON response: " + glz::format_error(parseResult, *response);
        } else if (GetExecutableAssets(release).empty()) {
            error = "Release " + release.tag_name + " carries no executable";
        } else {
            hasUpdate = Version(release.tag_name) > g_AppVersion;
        }

        Dispatcher::ToUi([this, callback = std::move(callback), release = std::move(release),
                          hasUpdate, error = std::move(error)]() mutable {
            if (!error.empty()) {
                callback(false, error);
                return;
            }

            _latestRelease = std::move(release);
            // A skipped version is no update, and no error either
            _hasUpdate = hasUpdate && !IsVersionSkipped(_latestRelease.tag_name);
            callback(_hasUpdate, "");
        });
    });
}

void UpdateManager::ShowUpdateNotification() {
    if (!_hasUpdate) {
        return;
    }

    // If auto-update is enabled, skip the dialog and install directly
    if (_cfg.Core.AutoUpdate) {
        LOG_INFO("Auto-update enabled - installing update automatically");
        DownloadAndInstallUpdate();
        return;
    }

    // Custom dialog rather than a message box, for button names that say what they do
    const INT_PTR result = DialogBoxParamW(GetModuleHandleW(nullptr),
                                           MAKEINTRESOURCEW(IDD_UPDATE_DIALOG),
                                           nullptr,
                                           UpdateDialogProc,
                                           reinterpret_cast<LPARAM>(this));

    switch (result) {
        case IDC_UPDATE_INSTALL:
            DownloadAndInstallUpdate();
            break;
        case IDC_UPDATE_SKIP:
            SkipVersion();
            break;
        default:
            // Remind later
            break;
    }
}

void UpdateManager::DownloadAndInstallUpdate() {
    const auto assets = GetExecutableAssets(_latestRelease);
    if (!_hasUpdate || assets.empty()) {
        MessageBoxW(nullptr, L"No update available or no assets found.", L"Update Error", MB_ICONERROR);
        return;
    }

    Stop();
    _updateWorker = std::thread([url = assets[0].browser_download_url, name = assets[0].name] {
        const std::wstring downloadPath = DownloadFile(url, name);
        Dispatcher::ToUi([downloadPath] {
            if (downloadPath.empty() || !ApplyUpdate(downloadPath)) {
                MessageBoxW(nullptr, L"The update could not be downloaded or installed.", L"Update Error",
                            MB_ICONERROR);
            }
        });
    });
}

std::vector<GitHubAsset> UpdateManager::GetExecutableAssets(const GitHubRelease& release) {
    std::vector<GitHubAsset> exeAssets;
    for (const auto& asset : release.assets) {
        if (asset.name.ends_with(".exe")) {
            exeAssets.push_back(asset);
        }
    }
    return exeAssets;
}

std::wstring UpdateManager::DownloadFile(const std::string& url, const std::string& filename) {
    wchar_t tempPath[MAX_PATH];
    const DWORD tempLength = GetTempPathW(MAX_PATH, tempPath);
    if (tempLength == 0 || tempLength > MAX_PATH) {
        LOG_ERROR("Update download: no temp directory (0x%08lX)", GetLastError());
        return {};
    }

    // Everything narrow in this app is UTF-8, so the name has to go through the one converter -
    // widening it byte by byte turns any non-ASCII asset name into a path that does not exist
    const std::wstring downloadPath = std::wstring(tempPath, tempLength) + Str::Utf8ToWide(filename);

    const std::optional<std::string> body = Fetch(url);
    if (!body || !File::Write(downloadPath.c_str(), *body)) {
        LOG_ERROR("Update download failed");
        DeleteFileW(downloadPath.c_str());
        return {};
    }
    return downloadPath;
}

bool UpdateManager::ApplyUpdate(const std::wstring& filePath) {
    wchar_t currentExecutable[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, currentExecutable, MAX_PATH);
    if (length == 0 || length == MAX_PATH) {
        return false;
    }

    const std::wstring target = PsQuote(std::wstring(currentExecutable, length));
    const std::wstring downloaded = PsQuote(filePath);

    // The copy has to wait for this process to let go of its own image
    const std::wstring command = L"Start-Sleep -Seconds 2; "
        L"Copy-Item -Path " + downloaded + L" -Destination " + target + L" -Force; "
        L"Remove-Item -Path " + downloaded + L" -Force; "
        L"Start-Sleep -Seconds 1; "
        L"Start-Process -FilePath " + target + L";";

    const std::wstring arguments = L"-WindowStyle Hidden -ExecutionPolicy Bypass -Command \"" + command + L"\"";

    SHELLEXECUTEINFOW sei = {sizeof(sei)};
    sei.fMask = SEE_MASK_NOASYNC;
    sei.lpVerb = L"open";
    sei.lpFile = L"powershell.exe";
    sei.lpParameters = arguments.c_str();
    sei.nShow = SW_HIDE;

    if (!ShellExecuteExW(&sei)) {
        LOG_ERROR("Failed to start the update process: 0x%08lX", GetLastError());
        return false;
    }

    // The updater waits before replacing the executable, so leave through the normal message loop.
    PostQuitMessage(0);
    return true;
}

bool UpdateManager::IsVersionSkipped(const std::string& version) const {
    return _cfg.Core.SkippedVersions.contains(version);
}

void UpdateManager::SkipVersion() {
    if (!_latestRelease.tag_name.empty()) {
        _cfg.Core.SkippedVersions.insert(_latestRelease.tag_name);
        _cfg.Save();
        LOG_INFO("Skipped version: %s", _latestRelease.tag_name.c_str());
    }
}

INT_PTR CALLBACK UpdateManager::UpdateDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_INITDIALOG) {
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, lParam);
        const auto* manager = reinterpret_cast<UpdateManager*>(lParam);

        SetDlgItemTextW(hDlg, IDC_UPDATE_VERSION, Str::Utf8ToWide(manager->_latestRelease.tag_name).c_str());
        SetDlgItemTextW(hDlg, IDC_UPDATE_NOTES,
                        Str::Utf8ToWide(manager->_latestRelease.body.empty()
                                            ? "No release notes available."
                                            : manager->_latestRelease.body).c_str());

        DialogControls::CenterOnScreen(hDlg);
        return TRUE;
    }

    if (message == WM_CLOSE) {
        EndDialog(hDlg, IDC_UPDATE_LATER);
        return TRUE;
    }

    if (message != WM_COMMAND) {
        return FALSE;
    }

    switch (LOWORD(wParam)) {
        case IDC_UPDATE_INSTALL:
        case IDC_UPDATE_SKIP:
        case IDC_UPDATE_LATER:
            EndDialog(hDlg, LOWORD(wParam));
            return TRUE;
        case IDCANCEL:
            EndDialog(hDlg, IDC_UPDATE_LATER);
            return TRUE;
        default:
            return FALSE;
    }
}
