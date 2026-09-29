#include "Bindings.hpp"

#include <windows.h>

#include <utility>

#include "ActionRegistry.hpp"
#include "Feedback.hpp"
#include "Foreground.hpp"
#include "HotkeyService.hpp"
#include "KeyNames.hpp"

namespace Bindings {

namespace {

    HotkeyService::HotkeyBinding _hotkey(const Binding& entry, const ActionDesc& desc,
                                         const ActionContext& context, ActionFn handler) {
        const bool edits = HasFlag(desc.Flags, ActionFlags::EditsText);
        const bool announcesItself = HasFlag(desc.Flags, ActionFlags::RunsCommand);
        // An edit's handler runs on the edit lane: the wrapper only plays its sound and text once it lands
        ActionFn run = context.Fb.Wrap(edits ? ActionFn{} : std::move(handler), entry.Sound, entry.SoundVolume,
                                       announcesItself ? std::string{} : context.Notification);

        // The key that fires an edit must not reach the app: the edit assumes the text still ends where the word did
        if (edits) {
            return {.onPress = std::move(run), .onEdit = std::move(handler), .block = true};
        }
        if (desc.MakeRelease) {
            return {.onPress = std::move(run), .onRelease = desc.MakeRelease(context), .block = entry.Trigger.Block};
        }
        if (entry.Trigger.OnRelease) {
            return {.onRelease = std::move(run), .block = entry.Trigger.Block, .tapOnly = entry.Trigger.TapOnly};
        }
        return {.onPress = std::move(run), .block = entry.Trigger.Block};
    }

} // anonymous namespace

void Apply(const std::vector<Binding>& bindings, Feedback& feedback) {
    for (const Binding& entry : bindings) {
        const ActionDesc* const desc = ActionRegistry::Find(entry.ActionId);
        const uint64_t mask = KeyNames::Parse(entry.Trigger.Keys);
        if (!mask || !desc) {
            continue;
        }

        const std::string notification = entry.ShowNotification
            ? Feedback::Compose(entry.Notification, desc->DefaultNotification, entry.Name, KeyNames::Format(mask))
            : std::string{};
        const ActionContext context{entry.Args, notification, feedback};
        ActionFn handler = desc->Make(context);
        if (!handler) {
            continue;
        }

        HotkeyService::RegisterHotkey(mask, entry.Trigger.Presses ? entry.Trigger.Presses : 1,
                                      _hotkey(entry, *desc, context, std::move(handler)),
                                      Foreground::CanonicalApp(entry.Trigger.App));
    }

    HotkeyService::Publish();
}

}
