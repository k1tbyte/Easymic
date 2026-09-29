#pragma once

#include <string>
#include <string_view>
#include <windows.h>

/// A std::string is UTF-8 everywhere (glaze stores bytes); Windows calls go wide through these two.
namespace Str {

    inline std::wstring Lower(const std::wstring_view text) {
        std::wstring out(text);
        if (!out.empty()) {
            CharLowerBuffW(out.data(), static_cast<DWORD>(out.size()));
        }
        return out;
    }

    /// Resumes after each replacement, so a `to` that contains `from` terminates.
    inline std::string Replace(std::string text, const std::string_view from, const std::string_view to) {
        if (from.empty()) {
            return text;
        }

        for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) {
            text.replace(at, from.size(), to);
        }
        return text;
    }

    inline std::wstring Utf8ToWide(const std::string& text) {
        if (text.empty()) {
            return {};
        }
        const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
        if (size <= 1) {
            return {};
        }

        std::wstring result(size - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), size);
        return result;
    }

    inline std::string WideToUtf8(const std::wstring& text) {
        if (text.empty()) {
            return {};
        }
        const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (size <= 1) {
            return {};
        }

        std::string result(size - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, result.data(), size, nullptr, nullptr);
        return result;
    }

}

