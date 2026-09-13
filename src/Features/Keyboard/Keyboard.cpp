#include "Keyboard.hpp"

#include "AppConfig.hpp"
#include "Core/ActionRegistry.hpp"
#include "Core/Overlay.hpp"
#include "Core/SettingsHost.hpp"
#include "InputLanguage.hpp"
#include "LayoutLayer.hpp"

namespace {

    constexpr ActionDesc Actions[] = {
        {.Id = "kbd.switch_layout",
         .Title = "Switch language",
         .Group = "Keyboard",
         .ArgsLabel = "Locales",
         .ArgsHint = "en, ru - empty for all",
         .Make = [](const ActionContext& context) -> ActionFn {
             return [locales = context.Args] { InputLanguage::SwitchNext(locales); };
         }},
    };

    constexpr SettingsRow PageRows[] = {
        // Relaid out at once, so the pill shows up in the preview the user is positioning
        {.Kind = RowKind::Check, .Label = L"Show the current layout on the overlay",
         .Field = Bind<&AppConfig::Keyboard, &KeyboardSettings::ShowLayout>(),
         .Changed = [](HWND, AppConfig&) { Overlay::Changed(); }},
    };

} // anonymous namespace

namespace Keyboard {

    void Register(Host& host) {
        for (const auto& desc : Actions) {
            ActionRegistry::Add(desc);
        }
        SettingsHost::AddPage({.Title = L"Keyboard", .Rows = PageRows});

        LayoutLayer::Register(host.Config.Keyboard);
    }
}
