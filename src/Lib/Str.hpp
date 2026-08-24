#ifndef EASYMIC_STR_HPP
#define EASYMIC_STR_HPP

#include <string>
#include <windows.h>

/**
 * @brief The one narrow<->wide conversion pair in the app.
 *
 * Narrow strings here are CP_ACP, never UTF-8: they come from and go to ANSI edit controls and
 * the *A Win32 entry points (PlaySoundA, ShellExecuteA, GetOpenFileNameA), so a UTF-8 decode
 * would mangle every non-Latin path the user picks.
 */
namespace Str {

    inline std::wstring ToWide(const std::string& text) {
        const int size = MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, nullptr, 0);
        if (size <= 1) {
            return {};
        }

        std::wstring result(size - 1, L'\0');
        MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, result.data(), size);
        return result;
    }

    inline std::string ToNarrow(const std::wstring& text) {
        const int size = WideCharToMultiByte(CP_ACP, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (size <= 1) {
            return {};
        }

        std::string result(size - 1, '\0');
        WideCharToMultiByte(CP_ACP, 0, text.c_str(), -1, result.data(), size, nullptr, nullptr);
        return result;
    }
}

#endif //EASYMIC_STR_HPP
