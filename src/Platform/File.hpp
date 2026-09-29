#pragma once

#include <windows.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

/// Whole files on Win32 handles: iostreams would bring their locale machinery into the binary.
namespace File {

    /// The whole file, or its first `limit` bytes.
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

    /// Replaces the file, or appends to it.
    inline bool Write(const wchar_t* path, const std::string_view bytes, const bool append = false) {
        const HANDLE file = CreateFileW(path, append ? FILE_APPEND_DATA : GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                        append ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            return false;
        }
        DWORD written = 0;
        const bool done = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr)
                          && written == bytes.size();
        CloseHandle(file);
        return done;
    }

    /// `name` in the exe's folder; the working directory when the exe path is unknown.
    inline std::wstring NextToExe(const std::wstring_view name) {
        wchar_t path[MAX_PATH];
        const std::wstring_view exe(path, GetModuleFileNameW(nullptr, path, MAX_PATH));
        return std::wstring(exe.substr(0, exe.find_last_of(L'\\') + 1)).append(name);
    }
}
