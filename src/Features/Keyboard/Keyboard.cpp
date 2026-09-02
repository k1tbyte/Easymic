#include "Keyboard.hpp"

#include "Core/ActionRegistry.hpp"
#include "InputLanguage.hpp"

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

} // anonymous namespace

namespace Keyboard {

    void Register(Host&) {
        for (const auto& desc : Actions) {
            ActionRegistry::Add(desc);
        }
    }
}
