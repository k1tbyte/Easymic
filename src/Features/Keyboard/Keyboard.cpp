#include "Keyboard.hpp"

#include <string>

#include "AppConfig.hpp"
#include "Autocorrect.hpp"
#include "Core/ActionRegistry.hpp"
#include "Core/Input/Input.hpp"
#include "InputLanguage.hpp"
#include "KeyboardPage.hpp"
#include "LayoutLayer.hpp"
#include "Learning.hpp"
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
        {.Id = "kbd.undo_auto_convert",
         .Title = "Undo auto-convert",
         .Group = "Keyboard",
         .Flags = ActionFlags::EditsText,
         .Make = [](const ActionContext&) -> ActionFn {
             return [] { WordTracker::UndoAutoConvert(Input::CurrentHold()); };
         }},
    };

} // anonymous namespace

namespace Keyboard {

    void Register(Host& host) {
        for (const auto& desc : Actions) {
            ActionRegistry::Add(desc);
        }
        KeyboardPage::Register(host.Config);

        LayoutLayer::Register(host.Config.Keyboard);
        Learning::Register(host.Config);
        Autocorrect::Register(host.Config.Keyboard, host.Fb);
        WordTracker::Register(host.Config.Keyboard);
    }
}
