#include "KeyboardPage.hpp"

#include "AppConfig.hpp"
#include "Core/Lifecycle.hpp"
#include "Core/Overlay.hpp"
#include "Core/SettingsHost.hpp"
#include "InputLanguage.hpp"
#include "KeyboardPacks.hpp"
#include "Settings/ExcludedApps.hpp"
#include "Settings/RulesPage.hpp"
#include "Str.hpp"

#include <algorithm>
#include <commctrl.h>
#include <string>
#include <vector>

namespace {

    AppConfig* _config = nullptr;
    bool _pageReady = false;
    std::vector<KeyboardPacks::PackInfo> _discovered;
    std::string _shownPairA, _shownPairB, _shownPackA, _shownPackB;
    bool _defaultA = false, _defaultB = false;

    std::vector<InputLanguage::Layout> _layouts;
    std::vector<std::wstring> _titles;
    std::vector<const wchar_t*> _items;
    std::vector<KeyboardPacks::PackInfo> _packsA, _packsB;

    void _refreshPacks(const AppConfig& cfg);

    std::span<const wchar_t* const> _layoutItems() {
        if (!_pageReady) {
            _layouts = InputLanguage::Installed();
            _titles.clear();
            for (const auto& layout : _layouts) {
                _titles.push_back(InputLanguage::Title(layout));
            }
            _items.clear();
            for (const auto& title : _titles) _items.push_back(title.c_str());
            _discovered = KeyboardPacks::Discover();
            _refreshPacks(*_config);
            _pageReady = true;
        } else if (_config->Keyboard.PairA != _shownPairA || _config->Keyboard.PairB != _shownPairB
                   || _config->Keyboard.PackA != _shownPackA || _config->Keyboard.PackB != _shownPackB) {
            _refreshPacks(*_config);
        }
        return _items;
    }

    int _findPack(const std::vector<KeyboardPacks::PackInfo>& packs, const std::string& packName);

    bool _canToggleAutoCorrect(const AppConfig& cfg) {
        const int a = InputLanguage::Find(_layouts, cfg.Keyboard.PairA, 0);
        const int b = InputLanguage::Find(_layouts, cfg.Keyboard.PairB, 1);
        const bool readyA = cfg.Keyboard.PackA.empty() ? _defaultA
            : _findPack(_packsA, cfg.Keyboard.PackA) <= static_cast<int>(_packsA.size());
        const bool readyB = cfg.Keyboard.PackB.empty() ? _defaultB
            : _findPack(_packsB, cfg.Keyboard.PackB) <= static_cast<int>(_packsB.size());
        return cfg.Keyboard.AutoCorrect != AutoCorrectMode::Off || (a >= 0 && b >= 0 && a != b
               && _layouts[a].Handle != _layouts[b].Handle && readyA && readyB);
    }

    std::span<const wchar_t* const> _autoCorrectItems() {
        static constexpr const wchar_t* items[] = {L"Off (hotkey only)", L"At the end of a word", L"While typing"};
        return items;
    }

    template <std::string KeyboardSettings::*Field, size_t Fallback>
    constexpr RowField PairField() {
        return {.Get = [](const AppConfig& cfg) { return InputLanguage::Find(_layouts, cfg.Keyboard.*Field, Fallback); },
                .Set = [](AppConfig& cfg, const int index) {
                    if (index >= 0 && index < static_cast<int>(_layouts.size())) {
                        cfg.Keyboard.*Field = _layouts[index].Id;
                    }
                }};
    }

    // Pack combos per side
    std::vector<std::wstring> _packTitlesA, _packTitlesB;
    std::vector<const wchar_t*> _packItemsA, _packItemsB;

    int _findPack(const std::vector<KeyboardPacks::PackInfo>& packs, const std::string& packName) {
        if (packName.empty()) return 0;
        const std::wstring wide = Str::Utf8ToWide(packName);
        for (size_t i = 0; i < packs.size(); ++i) {
            if (packs[i].Filename == wide) return static_cast<int>(i + 1);
        }
        return static_cast<int>(packs.size() + 1);
    }

    void _buildPackItems(const std::vector<KeyboardPacks::PackInfo>& packs, const HKL layout,
                         const bool available, const std::string& selected,
                         std::vector<std::wstring>& titles, std::vector<const wchar_t*>& items) {
        titles.clear();
        items.clear();
        const std::wstring name = layout ? Str::Utf8ToWide(KeyboardPacks::Locale(layout)) + L".pack" : L"";
        titles.push_back(layout ? L"Auto: " + name + (available ? L"" : L" (missing)") : L"Choose a layout");
        for (const auto& pack : packs) titles.push_back(pack.Filename);
        if (!selected.empty() && _findPack(packs, selected) > static_cast<int>(packs.size())) {
            titles.push_back(L"Missing or incompatible: " + Str::Utf8ToWide(selected));
        }
        for (const auto& title : titles) items.push_back(title.c_str());
    }

    void _refreshPacks(const AppConfig& cfg) {
        const int a = InputLanguage::Find(_layouts, cfg.Keyboard.PairA, 0);
        const int b = InputLanguage::Find(_layouts, cfg.Keyboard.PairB, 1);
        const auto compatible = [&](const int index) {
            std::vector<KeyboardPacks::PackInfo> result;
            if (index < 0) return result;
            const std::string locale = KeyboardPacks::Locale(_layouts[index].Handle);
            for (const auto& pack : _discovered) {
                if (pack.Valid && pack.EmbeddedLocale == locale) result.push_back(pack);
            }
            return result;
        };
        _packsA = compatible(a);
        _packsB = compatible(b);
        // Auto is `<locale>.pack`, one of the compatible
        const auto hasAuto = [&](const int index, const std::vector<KeyboardPacks::PackInfo>& packs) {
            return index >= 0 && _findPack(packs, KeyboardPacks::Locale(_layouts[index].Handle) + ".pack")
                                     <= static_cast<int>(packs.size());
        };
        _defaultA = hasAuto(a, _packsA);
        _defaultB = hasAuto(b, _packsB);
        _buildPackItems(_packsA, a >= 0 ? _layouts[a].Handle : nullptr, _defaultA,
                        cfg.Keyboard.PackA, _packTitlesA, _packItemsA);
        _buildPackItems(_packsB, b >= 0 ? _layouts[b].Handle : nullptr, _defaultB,
                        cfg.Keyboard.PackB, _packTitlesB, _packItemsB);
        _shownPairA = cfg.Keyboard.PairA;
        _shownPairB = cfg.Keyboard.PairB;
        _shownPackA = cfg.Keyboard.PackA;
        _shownPackB = cfg.Keyboard.PackB;
    }

    std::span<const wchar_t* const> _packItemsAFn() { return _packItemsA; }
    std::span<const wchar_t* const> _packItemsBFn() { return _packItemsB; }

    template <std::string KeyboardSettings::*Field, std::vector<KeyboardPacks::PackInfo>* Packs>
    constexpr RowField PackField() {
        return {.Get = [](const AppConfig& cfg) { return _findPack(*Packs, cfg.Keyboard.*Field); },
                .Set = [](AppConfig& cfg, const int index) {
                    if (index == 0) cfg.Keyboard.*Field = {};
                    else if (index > 0 && index <= static_cast<int>(Packs->size()))
                        cfg.Keyboard.*Field = Str::WideToUtf8((*Packs)[index - 1].Filename);
                }};
    }

    constexpr SettingsRow GeneralRows[] = {
        {.Kind = RowKind::Check, .Label = L"Show the current layout on the overlay",
         .Field = Bind<&AppConfig::Keyboard, &KeyboardSettings::ShowLayout>(),
         .Changed = [](HWND, AppConfig&) { Overlay::Changed(); }},
        {.Kind = RowKind::Combo, .Label = L"Convert words between",
         .Field = PairField<&KeyboardSettings::PairA, 0>(), .Items = &_layoutItems},
        {.Kind = RowKind::Combo, .Field = PackField<&KeyboardSettings::PackA, &_packsA>(), .Items = &_packItemsAFn,
         .Beside = true},
        {.Kind = RowKind::Combo, .Label = L"and",
         .Field = PairField<&KeyboardSettings::PairB, 1>(), .Items = &_layoutItems},
        {.Kind = RowKind::Combo, .Field = PackField<&KeyboardSettings::PackB, &_packsB>(), .Items = &_packItemsBFn,
         .Beside = true},
        {.Kind = RowKind::Combo, .Label = L"Autocorrect",
         .Field = Bind<&AppConfig::Keyboard, &KeyboardSettings::AutoCorrect>(), .Items = &_autoCorrectItems,
         .Enabled = &_canToggleAutoCorrect},
        {.Kind = RowKind::Check, .Label = L"Don't convert in password fields",
         .Field = Bind<&AppConfig::Keyboard, &KeyboardSettings::SkipPasswords>()},
        {.Kind = RowKind::Check, .Label = L"Don't convert in fullscreen apps (games)",
         .Field = Bind<&AppConfig::Keyboard, &KeyboardSettings::SkipFullscreen>()},
        {.Kind = RowKind::Check, .Label = L"Frequency analysis (off: rules and the dictionary only)",
         .Field = Bind<&AppConfig::Keyboard, &KeyboardSettings::FrequencyAnalysis>()},
        {.Kind = RowKind::Slider, .Label = L"Threshold (0 = pack)",
         .Field = Bind<&AppConfig::Keyboard, &KeyboardSettings::Threshold>(), .Min = 0, .Max = 200,
         .Enabled = [](const AppConfig& cfg) { return cfg.Keyboard.FrequencyAnalysis; }},
        {.Kind = RowKind::SoundPicker, .Label = L"Sound on a fix",
         .Field = Bind<&AppConfig::Keyboard, &KeyboardSettings::FixSound>()},
        {.Kind = RowKind::Slider, .Label = L"Fix sound volume",
         .Field = Bind<&AppConfig::Keyboard, &KeyboardSettings::FixSoundVolume>()},
        {.Kind = RowKind::Check, .Label = L"Show each fix as a notification",
         .Field = Bind<&AppConfig::Keyboard, &KeyboardSettings::FixNotification>()},
        {.Kind = RowKind::Check, .Label = L"Log typed words and decisions",
         .Field = Bind<&AppConfig::Keyboard, &KeyboardSettings::LogDecisions>()},
    };

    constexpr SettingsRow ExcludedAppsRows[] = {
        {.Kind = RowKind::Custom,
         .Create = [](HWND page, const RECT& cell, int id) { ExcludedApps::Create(page, cell, id, *_config); }},
    };

    constexpr SettingsRow RulesRows[] = {
        {.Kind = RowKind::Custom,
         .Create = [](HWND page, const RECT& cell, int id) { RulesPage::Create(page, cell, id, *_config); }},
    };

    constexpr SettingsTab Tabs[] = {
        {L"General", GeneralRows},
        {L"Excluded apps", ExcludedAppsRows},
        {L"Rules", RulesRows},
    };

} // anonymous namespace

namespace KeyboardPage {

    void Register(AppConfig& config) {
        _config = &config;
        Lifecycle::Suspend += [] { _pageReady = false; };
        SettingsHost::AddPage({.Title = L"Keyboard", .Tabs = Tabs});
    }
}
