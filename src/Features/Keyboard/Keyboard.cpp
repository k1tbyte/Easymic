#include "Keyboard.hpp"

#include <string>

#include "AppConfig.hpp"
#include "Autocorrect/Autocorrect.hpp"
#include "Core/ActionRegistry.hpp"
#include "Core/Input/Input.hpp"
#include "Core/Lifecycle.hpp"
#include "InputLanguage.hpp"
#include "KeyboardPage.hpp"
#include "LayoutLayer.hpp"
#include "Autocorrect/Learning.hpp"
#include "Autocorrect/WordTracker.hpp"
#include "definitions.h"

namespace {

    const KeyboardSettings* _settings = nullptr;
    /// The convert hotkey is bound: words are tracked without autocorrect too.
    bool _needed = false;

    void _restore() {
        if (!_needed && _settings->AutoCorrect == AutoCorrectMode::Off) {
            return;
        }
        auto runtime = Autocorrect::Resolve(*_settings);
        if (!runtime) {
            LOG_WARNING("Keyboard: the conversion pair is not two installed layouts");
            return;
        }
        const bool on = _needed || runtime->Auto;
        WordTracker::Use(std::move(runtime), on);
    }

    void _suspend() {
        _needed = false;
        WordTracker::Use(nullptr, false);
    }

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
             _needed = true;
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
        _settings = &host.Config.Keyboard;
        WordTracker::Register();
        Lifecycle::Restore += &_restore;
        Lifecycle::Suspend += &_suspend;
    }
}
