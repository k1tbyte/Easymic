#include "Keyboard.hpp"

#include <string>
#include <vector>

#include "AppConfig.hpp"
#include "Core/ActionRegistry.hpp"
#include "Core/Input/Input.hpp"
#include "Core/Overlay.hpp"
#include "Core/SettingsHost.hpp"
#include "InputLanguage.hpp"
#include "LayoutLayer.hpp"
#include "WordTracker.hpp"

namespace {

    constexpr ActionDesc Actions[] = {
        {.Id = "kbd.switch_layout",
         .Title = "Switch language",
         .Group = "Keyboard",
         .ArgsLabel = "Locales",
         .ArgsHint = "en, ru - empty for all",
         .Make = [](const ActionContext& context) -> ActionFn {
             return [locales = context.Args] {
                 if (const HKL next = InputLanguage::SwitchNext(locales)) {
                     LayoutLayer::Requested(next);
                 }
             };
         }},
        {.Id = "kbd.convert_word",
         .Title = "Convert the last word",
         .Group = "Keyboard",
         .Flags = ActionFlags::EditsText,
         .Make = [](const ActionContext&) -> ActionFn {
             WordTracker::Needed();
             return [] { WordTracker::ConvertWord(Input::CurrentHold()); };
         }},
    };

    /// What the two pair combos list, rebuilt with the page; Get and Set map through it.
    std::vector<InputLanguage::Layout> _layouts;
    std::vector<std::wstring> _titles;
    std::vector<const wchar_t*> _items;

    std::span<const wchar_t* const> _layoutItems() {
        _layouts = InputLanguage::Installed();
        _titles.clear();
        for (const auto& layout : _layouts) {
            _titles.push_back(InputLanguage::Title(layout));
        }
        _items.clear();
        for (const auto& title : _titles) {
            _items.push_back(title.c_str());
        }
        return _items;
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

    constexpr SettingsRow PageRows[] = {
        // Relaid out at once, so the pill shows up in the preview the user is positioning
        {.Kind = RowKind::Check, .Label = L"Show the current layout on the overlay",
         .Field = Bind<&AppConfig::Keyboard, &KeyboardSettings::ShowLayout>(),
         .Changed = [](HWND, AppConfig&) { Overlay::Changed(); }},
        {.Kind = RowKind::Combo, .Label = L"Convert words between",
         .Field = PairField<&KeyboardSettings::PairA, 0>(), .Items = &_layoutItems},
        {.Kind = RowKind::Combo, .Label = L"and",
         .Field = PairField<&KeyboardSettings::PairB, 1>(), .Items = &_layoutItems},
    };

} // anonymous namespace

namespace Keyboard {

    void Register(Host& host) {
        for (const auto& desc : Actions) {
            ActionRegistry::Add(desc);
        }
        SettingsHost::AddPage({.Title = L"Keyboard", .Rows = PageRows});

        LayoutLayer::Register(host.Config.Keyboard);
        WordTracker::Register(host.Config.Keyboard);
    }
}
