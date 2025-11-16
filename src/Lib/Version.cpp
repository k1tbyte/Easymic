#include "Version.hpp"
#include <sstream>
#include <algorithm>

// Global version instance
Version g_AppVersion;

Version::Version() {
    // Initialize with current application version
    *this = LoadFromVersionResource();
}

Version::Version(int major, int minor, int patch, int build)
    : major_(major), minor_(minor), patch_(patch), build_(build) {
}

Version::Version(const std::string& versionString) {
    ParseVersionString(versionString);
}

std::string Version::GetFormatted() const {
    if (build_ == 0) {
        return std::to_string(major_) + "." + std::to_string(minor_) + "." + std::to_string(patch_);
    }
    return std::to_string(major_) + "." + std::to_string(minor_) + "." + 
           std::to_string(patch_) + "." + std::to_string(build_);
}

std::string Version::GetShortFormat() const {
    return std::to_string(major_) + "." + std::to_string(minor_) + "." + std::to_string(patch_);
}

std::string Version::GetFullFormat() const {
    return std::to_string(major_) + "." + std::to_string(minor_) + "." + 
           std::to_string(patch_) + "." + std::to_string(build_);
}

bool Version::operator>(const Version& other) const {
    if (major_ != other.major_) return major_ > other.major_;
    if (minor_ != other.minor_) return minor_ > other.minor_;
    if (patch_ != other.patch_) return patch_ > other.patch_;
    return build_ > other.build_;
}

bool Version::operator<(const Version& other) const {
    return other > *this;
}

bool Version::operator==(const Version& other) const {
    return major_ == other.major_ && minor_ == other.minor_ && 
           patch_ == other.patch_ && build_ == other.build_;
}

bool Version::operator!=(const Version& other) const {
    return !(*this == other);
}

Version Version::GetCurrentVersion() {
    return LoadFromVersionResource();
}

void Version::ParseVersionString(const std::string& versionString) {
    // Handle version strings like "v1.2.3" or "1.2.3-beta"
    std::string cleanVersion = versionString;
    
    // Remove 'v' prefix if present
    if (!cleanVersion.empty() && cleanVersion[0] == 'v') {
        cleanVersion = cleanVersion.substr(1);
    }
    
    // Check for pre-release tag
    size_t dashPos = cleanVersion.find('-');
    if (dashPos != std::string::npos) {
        isPreRelease_ = true;
        preReleaseTag_ = cleanVersion.substr(dashPos + 1);
        cleanVersion = cleanVersion.substr(0, dashPos);
    }
    
    // Parse version numbers
    std::istringstream iss(cleanVersion);
    std::string token;
    std::vector<int> parts;
    
    while (std::getline(iss, token, '.')) {
        if (!token.empty()) {
            parts.push_back(std::stoi(token));
        }
    }
    
    if (parts.size() >= 1) major_ = parts[0];
    if (parts.size() >= 2) minor_ = parts[1];
    if (parts.size() >= 3) patch_ = parts[2];
    if (parts.size() >= 4) build_ = parts[3];
}

Version Version::LoadFromVersionResource() {
    wchar_t modulePath[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, modulePath, MAX_PATH)) {
        return Version(1, 0, 0, 0);
    }

    DWORD verHandle = 0;
    DWORD verSize = GetFileVersionInfoSizeW(modulePath, &verHandle);
    if (verSize == 0) {
        return Version(1, 0, 0, 0);
    }

    std::vector<BYTE> verData(verSize);
    if (!GetFileVersionInfoW(modulePath, verHandle, verSize, verData.data())) {
        return Version(1, 0, 0, 0);
    }

    VS_FIXEDFILEINFO* pFileInfo = nullptr;
    UINT puLenFileInfo = 0;
    if (!VerQueryValueW(verData.data(), L"\\", (VOID**)&pFileInfo, &puLenFileInfo)) {
        return Version(1, 0, 0, 0);
    }

    // Extract version numbers
    DWORD major = (pFileInfo->dwFileVersionMS >> 16) & 0xffff;
    DWORD minor = (pFileInfo->dwFileVersionMS) & 0xffff;
    DWORD patch = (pFileInfo->dwFileVersionLS >> 16) & 0xffff;
    DWORD build = (pFileInfo->dwFileVersionLS) & 0xffff;

    return Version(major, minor, patch, build);
}
