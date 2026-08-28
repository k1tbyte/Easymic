#include "Version.hpp"

#include <cstdlib>
#include <vector>
#include <windows.h>
#include <winver.h>

#pragma comment(lib, "version.lib")

namespace {
    /// What the app claims to be when the version resource cannot be read at all.
    constexpr int FallbackVersion[] = {1, 3, 0, 0};
}

// Global version instance
Version g_AppVersion;

Version::Version() : Version(LoadFromVersionResource()) {
}

Version::Version(int major, int minor, int patch, int build)
    : _major(major), _minor(minor), _patch(patch), _build(build) {
}

Version::Version(const std::string& versionString) {
    const char* cursor = versionString.c_str();

    if (*cursor == 'v') {
        cursor++;
    }

    for (int* component : {&_major, &_minor, &_patch, &_build}) {
        char* next = nullptr;
        const long parsed = strtol(cursor, &next, 10);

        // Stops on the '-' of a pre-release tag as well, which is exactly what we want
        if (next == cursor) {
            break;
        }

        *component = static_cast<int>(parsed);
        cursor = *next == '.' ? next + 1 : next;
    }
}

std::string Version::GetFullFormat() const {
    return std::to_string(_major) + "." + std::to_string(_minor) + "." +
           std::to_string(_patch) + "." + std::to_string(_build);
}

bool Version::operator>(const Version& other) const {
    if (_major != other._major) return _major > other._major;
    if (_minor != other._minor) return _minor > other._minor;
    if (_patch != other._patch) return _patch > other._patch;
    return _build > other._build;
}

Version Version::GetCurrentVersion() {
    return LoadFromVersionResource();
}

Version Version::LoadFromVersionResource() {
    const Version fallback(FallbackVersion[0], FallbackVersion[1], FallbackVersion[2], FallbackVersion[3]);

    wchar_t modulePath[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, modulePath, MAX_PATH)) {
        return fallback;
    }

    DWORD verHandle = 0;
    const DWORD verSize = GetFileVersionInfoSizeW(modulePath, &verHandle);
    if (verSize == 0) {
        return fallback;
    }

    std::vector<BYTE> verData(verSize);
    if (!GetFileVersionInfoW(modulePath, verHandle, verSize, verData.data())) {
        return fallback;
    }

    VS_FIXEDFILEINFO* fileInfo = nullptr;
    UINT infoLength = 0;
    if (!VerQueryValueW(verData.data(), L"\\", (VOID**)&fileInfo, &infoLength) || !fileInfo) {
        return fallback;
    }

    return {static_cast<int>((fileInfo->dwFileVersionMS >> 16) & 0xffff),
            static_cast<int>(fileInfo->dwFileVersionMS & 0xffff),
            static_cast<int>((fileInfo->dwFileVersionLS >> 16) & 0xffff),
            static_cast<int>(fileInfo->dwFileVersionLS & 0xffff)};
}
