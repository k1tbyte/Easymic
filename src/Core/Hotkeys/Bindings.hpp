#pragma once

#include <utility>
#include <vector>

#include "ActionRegistry.hpp"
#include "AppConfig.hpp"
#include "Feedback.hpp"
#include "HotkeyService.hpp"
#include "KeyNames.hpp"

/// Turning what the config says into what the hotkey service holds - the one place that knows
/// both shapes, and the only reason either has to know the other exists.
namespace Bindings {

    /**
     * @brief Registers every configured binding that can actually be registered.
     *
     * A configured trigger is not a registered one: the combination can be missing or unparseable,
     * the id can name an action no module registered, and the action itself can decline the
     * argument it was given - hooking the desktop for any of those is not free. None of them drops
     * the binding; it stays in the list, visible in settings, and simply never fires.
     *
     * @return true when at least one hotkey took, which is what makes installing the hooks worth it.
     */
    inline bool Apply(const std::vector<Binding>& bindings, Feedback& feedback) {
        bool registered = false;

        for (const auto& entry : bindings) {
            const ActionDesc* const desc = ActionRegistry::Find(entry.ActionId);
            const uint64_t mask = KeyNames::Parse(entry.Trigger.Keys);
            if (!mask || !desc) {
                continue;
            }

            const std::string text = entry.ShowNotification
                ? Feedback::Compose(entry.Notification, desc->DefaultNotification, entry.Name,
                                    KeyNames::Format(mask))
                : std::string{};

            const ActionContext context{entry.Args, text, feedback};
            auto handler = desc->Make(context);
            if (!handler) {
                continue;
            }

            // An action that runs a command announces itself once the command answers, which can
            // be minutes later, so the wrapper must not announce it as well
            auto run = feedback.Wrap(std::move(handler), entry.Sound, entry.SoundVolume,
                                     HasFlag(desc->Flags, ActionFlags::RunsCommand)
                                         ? std::string{} : text);

            HotkeyService::HotkeyBinding hotkey{.block = entry.Trigger.Block};
            if (desc->MakeRelease) {
                hotkey.onPress = std::move(run);
                hotkey.onRelease = desc->MakeRelease(context);
            } else if (entry.Trigger.OnRelease) {
                hotkey.onRelease = std::move(run);
                hotkey.tapOnly = entry.Trigger.TapOnly;
            } else {
                hotkey.onPress = std::move(run);
            }

            registered |= HotkeyService::RegisterHotkey(
                mask, entry.Trigger.Presses ? entry.Trigger.Presses : 1, hotkey);
        }

        return registered;
    }
}
