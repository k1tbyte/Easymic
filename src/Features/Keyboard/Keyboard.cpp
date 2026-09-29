#include "Keyboard.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "AppConfig.hpp"
#include "Autocorrect/Autocorrect.hpp"
#include "Autocorrect/Learning.hpp"
#include "Autocorrect/WordTracker.hpp"
#include "Core/ActionRegistry.hpp"
#include "Core/Hotkeys/KeyNames.hpp"
#include "Core/Input/Input.hpp"
#include "Core/Lifecycle.hpp"
#include "InputLanguage.hpp"
#include "KeyboardPage.hpp"
#include "LayoutLayer.hpp"
#include "definitions.h"

namespace {

    constexpr std::string_view ConvertWordId = "kbd.convert_word";

    struct ResolveKey {
        KeyboardSettings Settings;
        std::vector<InputLanguage::Layout> Layouts;

        bool operator==(const ResolveKey&) const = default;
    };

    const AppConfig* _config = nullptr;
    std::optional<ResolveKey> _resolvedFor;
    std::shared_ptr<Autocorrect::Runtime> _resolved;

    bool _convertHotkeyBound() {
        return std::ranges::any_of(_config->Bindings, [](const Binding& binding) {
            return binding.ActionId == ConvertWordId && KeyNames::Parse(binding.Trigger.Keys) != 0;
        });
    }

    /// Fields Resolve never reads are cleared: editing them must not remap the packs.
    ResolveKey _resolveKey() {
        ResolveKey key{_config->Keyboard, InputLanguage::Installed()};
        key.Settings.Learned = {};
        key.Settings.ShowLayout = false;
        key.Settings.FixSound.clear();
        key.Settings.FixSoundVolume = 0;
        key.Settings.FixNotification = false;
        return key;
    }

    /// Resolve maps both packs and parses every rules file: only redone when its inputs changed. A failed pack
    /// load is retried, the files may have arrived since.
    std::shared_ptr<Autocorrect::Runtime> _resolve() {
        ResolveKey key = _resolveKey();
        if (_resolvedFor == key) {
            return _resolved;
        }
        _resolved = Autocorrect::Resolve(_config->Keyboard);
        const bool packsFailed = _resolved && !_resolved->Auto && key.Settings.AutoCorrect != AutoCorrectMode::Off;
        _resolvedFor = packsFailed ? std::nullopt : std::optional(std::move(key));
        return _resolved;
    }

    void _restore() {
        const bool convertBound = _convertHotkeyBound();
        if (!convertBound && _config->Keyboard.AutoCorrect == AutoCorrectMode::Off) {
            _resolvedFor.reset();
            _resolved.reset();
            return;
        }
        const auto runtime = _resolve();
        if (!runtime) {
            LOG_WARNING("Keyboard: the conversion pair is not two installed layouts");
            return;
        }
        WordTracker::Use(runtime, convertBound || runtime->Auto);
    }

    void _suspend() {
        WordTracker::Use(nullptr, false);
    }

    constexpr ActionDesc Actions[] = {
        {.Id = "kbd.switch_layout",
         .Title = "Switch language",
         .Group = "Keyboard",
         .ArgsLabel = "Locales",
         .ArgsHint = "en, ru - empty for all",
         .Make = [](const ActionContext& context) -> ActionFn {
             return [ring = InputLanguage::Ring(context.Args)] {
                 if (const HKL next = InputLanguage::SwitchNext(ring)) {
                     LayoutLayer::Requested(next);
                 }
             };
         }},
        {.Id = ConvertWordId,
         .Title = "Convert the last word",
         .Group = "Keyboard",
         .Flags = ActionFlags::EditsText,
         .Make = [](const ActionContext&) -> ActionFn {
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
        _config = &host.Config;
        WordTracker::Register();
        Lifecycle::Restore += &_restore;
        Lifecycle::Suspend += &_suspend;
    }
}
