#include "Version.hpp"

#include <cstdlib>
#include <vector>
#include <windows.h>
#include <winver.h>

#pragma comment(lib, "version.lib")

Version::Version(const std::string& versionString) {
    const char* cursor = versionString.c_str();

    if (*cursor == 'v') {
        cursor++;
    }

    for (int* component : {&_major, &_minor, &_patch, &_build}) {
        char* next = nullptr;
        const long parsed = strtol(cursor, &next, 10);

        if (next == cursor) {
            break;
        }

        *component = static_cast<int>(parsed);
        // strtol would read the "-1" of a pre-release suffix as minus one
        if (*next != '.') {
            break;
        }
        cursor = next + 1;
    }
}

const Version& Version::App() {
    static const Version version = LoadFromVersionResource();
    return version;
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

Version Version::LoadFromVersionResource() {
    Version version;

    wchar_t path[MAX_PATH];
    DWORD handle = 0;
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) {
        return version;
    }

    const DWORD size = GetFileVersionInfoSizeW(path, &handle);
    std::vector<BYTE> data(size);
    VS_FIXEDFILEINFO* info = nullptr;
    UINT length = 0;
    if (!size || !GetFileVersionInfoW(path, handle, size, data.data())
        || !VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &length) || !info) {
        return version;
    }

    version._major = HIWORD(info->dwFileVersionMS);
    version._minor = LOWORD(info->dwFileVersionMS);
    version._patch = HIWORD(info->dwFileVersionLS);
    version._build = LOWORD(info->dwFileVersionLS);
    return version;
}
