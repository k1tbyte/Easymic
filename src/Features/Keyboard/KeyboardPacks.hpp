#pragma once

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Convert/Pack.hpp"
#include "Convert/Rules.hpp"
#include "InputLanguage.hpp"
#include "Platform/File.hpp"
#include "Platform/Str.hpp"
#include "definitions.h"

namespace KeyboardPacks {

    constexpr std::wstring_view PackExtension = L".pack";
    constexpr size_t MaxFilename = 100;

    inline std::filesystem::path PacksDir() {
        return File::NextToExe(L"packs");
    }

    inline bool ValidPackFilename(const std::wstring_view name) {
        if (name.size() > MaxFilename || !name.ends_with(PackExtension)) {
            return false;
        }
        const auto stem = name.substr(0, name.size() - PackExtension.size());
        return !stem.empty() && std::ranges::all_of(stem, [](const wchar_t c) {
            return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9')
                   || c == L'-' || c == L'_';
        });
    }

    struct PackInfo {
        std::wstring Filename;
        std::string EmbeddedLocale;
        bool Valid;
    };

    inline bool Read(Convert::Pack& pack, const std::filesystem::path& path) {
        std::wstring error;
        if (pack.Load(path.wstring(), &error)) {
            return true;
        }
        LOG_ERROR("Keyboard: %s: %s", Str::WideToUtf8(path.filename().wstring()).c_str(), Str::WideToUtf8(error).c_str());
        return false;
    }

    inline std::vector<PackInfo> Discover() {
        std::vector<PackInfo> result;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(PacksDir(), ec)) {
            if (!entry.is_regular_file(ec)) {
                continue;
            }
            const std::wstring filename = entry.path().filename().wstring();
            if (!ValidPackFilename(filename)) {
                continue;
            }
            Convert::Pack pack;
            const bool ok = Read(pack, entry.path());
            result.push_back({filename, ok ? std::string(pack.Locale()) : std::string{}, ok});
        }
        std::ranges::sort(result, {}, &PackInfo::Filename);
        return result;
    }

    /// `packName` empty: the layout language's own pack.
    inline std::filesystem::path PackPath(const std::string_view packName, const std::string_view locale) {
        const std::string name = packName.empty() ? std::string(locale) + ".pack" : std::string(packName);
        const std::wstring wide = Str::Utf8ToWide(name);
        return ValidPackFilename(wide) ? PacksDir() / wide : std::filesystem::path{};
    }

    inline bool Load(Convert::Pack& pack, const HKL layout, const std::string_view packName = {}) {
        const std::string locale = InputLanguage::Iso639(layout);
        const auto path = PackPath(packName, locale);
        if (path.empty()) {
            LOG_ERROR("Keyboard: no valid pack '%s' for layout language '%s'", std::string(packName).c_str(), locale.c_str());
            return false;
        }
        if (!Read(pack, path)) {
            return false;
        }
        if (pack.Locale() != locale) {
            LOG_ERROR("Keyboard: %s is a '%s' pack, the layout speaks '%s'", Str::WideToUtf8(path.filename().wstring()).c_str(),
                      pack.Locale(), locale.c_str());
            return false;
        }
        return true;
    }

    inline void LoadRules(Convert::Rules& rules) {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(PacksDir(), ec)) {
            if (entry.path().extension() == L".rules") {
                rules.Load(entry.path());
            }
        }
    }
}
