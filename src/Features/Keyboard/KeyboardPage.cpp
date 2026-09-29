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
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {

    using PackList = std::vector<KeyboardPacks::PackInfo>;

    constexpr size_t SideCount = 2;
    constexpr std::string KeyboardSettings::* PairFields[SideCount] = {&KeyboardSettings::PairA, &KeyboardSettings::PairB};
    constexpr std::string KeyboardSettings::* PackFields[SideCount] = {&KeyboardSettings::PackA, &KeyboardSettings::PackB};

    struct PackSide {
        PackList Compatible;
        bool HasAuto = false;
        std::vector<std::wstring> Titles;
        std::vector<const wchar_t*> Items;
        std::string ShownPair;
        std::string ShownPack;
    };

    AppConfig* _config = nullptr;
    bool _pageReady = false;
    PackList _discovered;
    std::vector<InputLanguage::Layout> _layouts;
    std::vector<std::wstring> _titles;
    std::vector<const wchar_t*> _items;
    PackSide _sides[SideCount];

    int _layoutAt(const AppConfig& cfg, const size_t side) {
        return InputLanguage::Find(_layouts, cfg.Keyboard.*PairFields[side], side);
    }

    std::optional<size_t> _findPack(const PackList& packs, const std::string& name) {
        const auto it = std::ranges::find(packs, Str::Utf8ToWide(name), &KeyboardPacks::PackInfo::Filename);
        if (it == packs.end()) {
            return std::nullopt;
        }
        return static_cast<size_t>(it - packs.begin());
    }

    // 0 is Auto, then the compatible packs; one past them stands for a configured pack that is missing
    int _packComboIndex(const PackSide& side, const std::string& name) {
        if (name.empty()) {
            return 0;
        }
        const auto found = _findPack(side.Compatible, name);
        return static_cast<int>(found ? *found + 1 : side.Compatible.size() + 1);
    }

    bool _packReady(const PackSide& side, const std::string& name) {
        return name.empty() ? side.HasAuto : _findPack(side.Compatible, name).has_value();
    }

    bool _canToggleAutoCorrect(const AppConfig& cfg) {
        const int a = _layoutAt(cfg, 0);
        const int b = _layoutAt(cfg, 1);
        return cfg.Keyboard.AutoCorrect != AutoCorrectMode::Off
               || (a >= 0 && b >= 0 && _layouts[a].Handle != _layouts[b].Handle
                   && _packReady(_sides[0], cfg.Keyboard.PackA) && _packReady(_sides[1], cfg.Keyboard.PackB));
    }

    std::span<const wchar_t* const> _autoCorrectItems() {
        static constexpr const wchar_t* items[] = {L"Off (hotkey only)", L"At the end of a word", L"While typing"};
        return items;
    }

    void _refreshSide(const size_t index) {
        PackSide& side = _sides[index];
        const KeyboardSettings& settings = _config->Keyboard;
        const std::string& selected = settings.*PackFields[index];
        const int at = InputLanguage::Find(_layouts, settings.*PairFields[index], index);

        side.Compatible.clear();
        std::string locale;
        if (at >= 0) {
            locale = InputLanguage::Iso639(_layouts[at].Handle);
            std::ranges::copy_if(_discovered, std::back_inserter(side.Compatible), [&](const KeyboardPacks::PackInfo& pack) {
                return pack.Valid && pack.EmbeddedLocale == locale;
            });
        }
        side.HasAuto = at >= 0 && _findPack(side.Compatible, locale + ".pack");

        side.Titles.clear();
        side.Titles.push_back(at < 0 ? std::wstring(L"Choose a layout")
                                     : L"Auto: " + Str::Utf8ToWide(locale) + L".pack" + (side.HasAuto ? L"" : L" (missing)"));
        for (const auto& pack : side.Compatible) {
            side.Titles.push_back(pack.Filename);
        }
        if (!selected.empty() && !_findPack(side.Compatible, selected)) {
            side.Titles.push_back(L"Missing or incompatible: " + Str::Utf8ToWide(selected));
        }
        side.Items.clear();
        for (const auto& title : side.Titles) {
            side.Items.push_back(title.c_str());
        }
        side.ShownPair = settings.*PairFields[index];
        side.ShownPack = selected;
    }

    void _refreshPacks() {
        for (size_t side = 0; side < SideCount; ++side) {
            _refreshSide(side);
        }
    }

    bool _sidesChanged() {
        for (size_t side = 0; side < SideCount; ++side) {
            if (_config->Keyboard.*PairFields[side] != _sides[side].ShownPair
                || _config->Keyboard.*PackFields[side] != _sides[side].ShownPack) {
                return true;
            }
        }
        return false;
    }

    void _loadPage() {
        _layouts = InputLanguage::Installed();
        _titles.clear();
        for (const auto& layout : _layouts) {
            _titles.push_back(InputLanguage::Title(layout));
        }
        _items.clear();
        for (const auto& title : _titles) {
            _items.push_back(title.c_str());
        }
        _discovered = KeyboardPacks::Discover();
        _refreshPacks();
        _pageReady = true;
    }

    std::span<const wchar_t* const> _layoutItems() {
        if (!_pageReady) {
            _loadPage();
        } else if (_sidesChanged()) {
            _refreshPacks();
        }
        return _items;
    }

    template <size_t Side>
    std::span<const wchar_t* const> _packItems() {
        return _sides[Side].Items;
    }

    template <size_t Side>
    constexpr RowField PairField() {
        return {.Get = [](const AppConfig& cfg) { return _layoutAt(cfg, Side); },
                .Set = [](AppConfig& cfg, const int index) {
                    if (index >= 0 && index < static_cast<int>(_layouts.size())) {
                        cfg.Keyboard.*PairFields[Side] = _layouts[index].Id;
                    }
                }};
    }

    template <size_t Side>
    constexpr RowField PackField() {
        return {.Get = [](const AppConfig& cfg) { return _packComboIndex(_sides[Side], cfg.Keyboard.*PackFields[Side]); },
                .Set = [](AppConfig& cfg, const int index) {
                    const PackList& packs = _sides[Side].Compatible;
                    std::string& name = cfg.Keyboard.*PackFields[Side];
                    if (index == 0) {
                        name.clear();
                    } else if (index > 0 && index <= static_cast<int>(packs.size())) {
                        name = Str::WideToUtf8(packs[index - 1].Filename);
                    }
                }};
    }

    void _release() {
        _pageReady = false;
        _discovered = {};
        _layouts = {};
        _titles = {};
        _items = {};
        std::ranges::fill(_sides, PackSide{});
    }

    constexpr SettingsRow GeneralRows[] = {
        {.Kind = RowKind::Check, .Label = L"Show the current layout on the overlay",
         .Field = Bind<&AppConfig::Keyboard, &KeyboardSettings::ShowLayout>(),
         .Changed = [](HWND, AppConfig&) { Overlay::Changed(); }},
        {.Kind = RowKind::Combo, .Label = L"Convert words between",
         .Field = PairField<0>(), .Items = &_layoutItems},
        {.Kind = RowKind::Combo, .Field = PackField<0>(), .Items = &_packItems<0>, .Beside = true},
        {.Kind = RowKind::Combo, .Label = L"and",
         .Field = PairField<1>(), .Items = &_layoutItems},
        {.Kind = RowKind::Combo, .Field = PackField<1>(), .Items = &_packItems<1>, .Beside = true},
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
        Lifecycle::Restore += &_release;
        SettingsHost::AddPage({.Title = L"Keyboard", .Tabs = Tabs});
    }
}
