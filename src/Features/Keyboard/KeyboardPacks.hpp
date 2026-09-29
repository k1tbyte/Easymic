#pragma once

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Convert/Pack.hpp"
#include "Convert/Rules.hpp"
#include "Platform/File.hpp"
#include "Platform/Str.hpp"

namespace KeyboardPacks {

    inline std::string Locale(const HKL layout) {
        wchar_t name[16]{};
        if (!GetLocaleInfoW(MAKELCID(LOWORD(reinterpret_cast<UINT_PTR>(layout)), SORT_DEFAULT),
                            LOCALE_SISO639LANGNAME, name, static_cast<int>(std::size(name)))) {
            return {};
        }
        return Str::WideToUtf8(name);
    }

    inline std::filesystem::path PacksDir() {
        return File::NextToExe(L"packs");
    }

    inline bool ValidPackFilename(const std::wstring_view name) {
        if (name.size() < 6 || name.size() > 100) return false;
        const auto dot = name.rfind(L'.');
        if (dot == std::wstring_view::npos) return false;
        if (name.substr(dot) != L".pack") return false;
        const auto stem = name.substr(0, dot);
        if (stem.empty()) return false;
        for (const wchar_t c : stem) {
            if (!((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z')
                  || (c >= L'0' && c <= L'9') || c == L'-' || c == L'_'))
                return false;
        }
        return true;
    }

    struct PackInfo {
        std::wstring Filename;
        std::string EmbeddedLocale;
        bool Valid;
    };

    inline std::vector<PackInfo> Discover() {
        std::vector<PackInfo> result;
        const auto dir = PacksDir();
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) return result;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            const std::wstring filename = entry.path().filename().wstring();
            if (!ValidPackFilename(filename)) continue;
            Convert::Pack pack;
            const bool ok = pack.Load(entry.path().wstring());
            result.push_back({filename, ok ? std::string(pack.Locale()) : std::string{}, ok});
        }
        std::ranges::sort(result, {}, &PackInfo::Filename);
        return result;
    }

    inline std::wstring Path(const std::string_view locale) {
        const auto dir = PacksDir();
        return locale.empty() || dir.empty() ? std::wstring{}
            : (dir / (Str::Utf8ToWide(std::string(locale)) + L".pack")).wstring();
    }

    inline std::wstring ResolvePath(const std::string_view packName, const HKL layout) {
        if (packName.empty()) return Path(Locale(layout));
        const std::wstring wide = Str::Utf8ToWide(std::string(packName));
        const auto dir = PacksDir();
        if (!ValidPackFilename(wide) || dir.empty()) return {};
        return (dir / wide).wstring();
    }

    inline bool Load(Convert::Pack& pack, const HKL layout, const std::string_view packName = {}) {
        const std::string locale = Locale(layout);
        const std::wstring path = ResolvePath(packName, layout);
        return !path.empty() && pack.Load(path) && pack.Locale() == locale;
    }

    /// Every `packs/*.rules`, e.g. Punto's converted by `langpack rules`.
    inline void LoadRules(Convert::Rules& rules) {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(PacksDir(), ec)) {
            if (entry.path().extension() == L".rules") {
                rules.Load(entry.path());
            }
        }
    }
}
