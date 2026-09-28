#pragma once

#include <string>
#include <string_view>
#include <windows.h>

/**
 * @brief The one narrow<->wide conversion pair in the app.
 *
 * A narrow string is UTF-8, everywhere, no exceptions. It exists only because the config format
 * stores bytes - glaze serializes std::string and has no notion of std::wstring - so UTF-8 is
 * what a std::string in AppConfig means.
 *
 * Everything that talks to Windows goes wide: the *W entry points take what the OS actually
 * speaks, and these two functions are the whole border between the two worlds. A second narrow
 * encoding (CP_ACP) used to live here as well, which is how a name could round-trip through one
 * and a command line through the other.
 */
namespace Str {

    /// A lowercase copy, by the rules Windows compares names with.
    inline std::wstring Lower(const std::wstring_view text) {
        std::wstring out(text);
        if (!out.empty()) {
            CharLowerBuffW(out.data(), static_cast<DWORD>(out.size()));
        }
        return out;
    }

    /// Replaces every occurrence. Skipping past the replacement keeps `to` containing `from` safe.
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

