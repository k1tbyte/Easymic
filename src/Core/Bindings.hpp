#pragma once

#include <utility>
#include <vector>

#include "ActionRegistry.hpp"
#include "AppConfig.hpp"
#include "Feedback.hpp"
#include "HotkeyService.hpp"

/// Turning what the config says into what the hotkey service holds - the one place that knows
/// both shapes, and the only reason either has to know the other exists.
namespace Bindings {

    /**
     * @brief Registers every configured action that can actually be registered.
     *
     * A configured hotkey is not a registered one: the combination can be missing, the id can name
     * an action no module registered, and the action itself can decline the argument it was given -
     * hooking the desktop for any of those is not free.
     *
     * @return true when at least one hotkey took, which is what makes installing the hooks worth it.
     */
    inline bool Apply(const std::vector<Action>& actions, Feedback& feedback) {
        bool registered = false;

        for (const auto& action : actions) {
            const ActionDesc* const desc = ActionRegistry::Find(action.ActionId);
            if (!action.Hotkey || !desc) {
                continue;
            }

            const std::string text = action.ShowNotification
                ? Feedback::Compose(action.Notification, desc->DefaultNotification, action.Name,
                                    action.Hotkey)
                : std::string{};

            const ActionContext context{action.Args, text, feedback};
            auto handler = desc->Make(context);
            if (!handler) {
                continue;
            }

            // An action that runs a command announces itself once the command answers, which can
            // be minutes later, so the wrapper must not announce it as well
            auto run = feedback.Wrap(std::move(handler), action.Sound, action.SoundVolume,
                                     HasFlag(desc->Flags, ActionFlags::RunsCommand)
                                         ? std::string{} : text);

            HotkeyService::HotkeyBinding binding{.block = action.Block};
            if (desc->MakeRelease) {
                binding.onPress = std::move(run);
                binding.onRelease = desc->MakeRelease(context);
            } else if (action.OnRelease) {
                binding.onRelease = std::move(run);
                binding.tapOnly = action.TapOnly;
            } else {
                binding.onPress = std::move(run);
            }

            registered |= HotkeyService::RegisterHotkey(
                action.Hotkey, action.Presses ? action.Presses : 1, binding);
        }

        return registered;
    }
}
