#pragma once

#include <windows.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

/// Win32 handles rather than iostreams: their locale machinery would land in the binary.
namespace File {

    inline std::optional<std::string> Read(const wchar_t* path, const DWORD limit = MAXDWORD) {
        const HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            return std::nullopt;
        }
        std::optional<std::string> bytes;
        if (LARGE_INTEGER size{}; GetFileSizeEx(file, &size)) {
            bytes.emplace(static_cast<size_t>(std::min<LONGLONG>(size.QuadPart, limit)), '\0');
            DWORD read = 0;
            if (!ReadFile(file, bytes->data(), static_cast<DWORD>(bytes->size()), &read, nullptr)
                || read != bytes->size()) {
                bytes.reset();
            }
        }
        CloseHandle(file);
        return bytes;
    }

    /// Replacing goes through a .tmp beside the file, so a crash mid-write never leaves half of it.
    inline bool Write(const wchar_t* path, const std::string_view bytes, const bool append = false) {
        const std::wstring temp = append ? std::wstring{} : std::wstring(path) + L".tmp";
        const HANDLE file = CreateFileW(append ? path : temp.c_str(), append ? FILE_APPEND_DATA : GENERIC_WRITE,
                                        FILE_SHARE_READ, nullptr, append ? OPEN_ALWAYS : CREATE_ALWAYS,
                                        FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            return false;
        }
        DWORD written = 0;
        const bool done = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr)
                          && written == bytes.size() && (append || FlushFileBuffers(file));
        CloseHandle(file);
        return done && (append || MoveFileExW(temp.c_str(), path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH));
    }

    inline std::wstring NextToExe(const std::wstring_view name) {
        wchar_t path[MAX_PATH];
        const std::wstring_view exe(path, GetModuleFileNameW(nullptr, path, MAX_PATH));
        return std::wstring(exe.substr(0, exe.find_last_of(L'\\') + 1)).append(name);
    }
}
