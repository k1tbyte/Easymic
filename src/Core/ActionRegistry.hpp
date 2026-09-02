#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

class Feedback;

/// What an action does when its trigger fires. Runs on the Dispatcher worker, never in a hook.
using ActionFn = std::function<void()>;

enum class ActionFlags : uint32_t {
    None = 0,
    /// The action needs no sound of its own - muting already has its own feedback.
    NoSound = 1u << 0,
    /// The argument is a command line, so the command tokens apply to it and the dialog offers a
    /// command row rather than a plain text one.
    RunsCommand = 1u << 1,
};

constexpr ActionFlags operator|(const ActionFlags a, const ActionFlags b) {
    return static_cast<ActionFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr bool HasFlag(const ActionFlags value, const ActionFlags flag) {
    return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
}

/// What the factory below is given. Args and Notification are references the factory outlives, so
/// anything it needs past the call has to be copied - the config is rewritten on every Apply.
struct ActionContext {
    /// Whatever the user typed in this binding's argument row.
    const std::string& Args;
    /// What the binding announces, already composed. Empty when it announces nothing. Only an
    /// action that delivers its own text reads this; for the rest the kernel shows it.
    const std::string& Notification;
    Feedback& Fb;
};

/**
 * @brief One thing a trigger can run, as the module that owns it describes itself to the kernel.
 *
 * Make is a plain function pointer rather than a std::function: this stays POD, can be
 * constexpr, and costs nothing in the binary. An action cannot exist without its factory, which
 * is what makes "registered but not dispatchable" unrepresentable.
 */
struct ActionDesc {
    /// Config key, written to disk - permanent. Renaming one is a migration, not an edit.
    std::string_view Id;
    std::string_view Title;
    /// Groups the action in the "Add action" menu.
    std::string_view Group;
    ActionFlags Flags = ActionFlags::None;
    /// SoundCatalog key prefilled when the action is configured for the first time.
    std::string_view DefaultSound;
    /// Notification text prefilled the same way. Empty means the action stays silent on screen.
    std::string_view DefaultNotification;
    /// Label of the argument row, empty when the action takes none. The hint spells out the
    /// format, since every action reads its argument its own way.
    std::string_view ArgsLabel;
    std::string_view ArgsHint;
    /// Returns an empty function when the argument it was given cannot drive anything - that is
    /// how an action declines a binding, and the binding is then not registered at all.
    ActionFn (*Make)(const ActionContext&) = nullptr;
    /// The release edge, for an action that needs both of them (push to talk). Having one is what
    /// makes "trigger on release" meaningless for the action, so nothing else has to say so.
    ActionFn (*MakeRelease)(const ActionContext&) = nullptr;
};

/**
 * @brief Every action any module has registered.
 *
 * Enumerable rather than lookup-only: the "Add action" menu is built by walking this and grouping
 * by Group. The order is the order modules registered in, which is their order in main - stable
 * across runs, so the menu does not reshuffle itself between launches.
 */
namespace ActionRegistry {

    /**
     * @brief What a notification says when the action has no default of its own.
     *
     * {name} and {key} are resolved when the hotkey is registered, {volume}, {mic} and {bell}
     * when it fires - see Feedback.
     */
    inline constexpr char DefaultNotification[] = "{name} triggered";

    inline std::vector<ActionDesc> All;

    inline void Add(const ActionDesc& desc) {
        All.push_back(desc);
    }

    inline const ActionDesc* Find(const std::string_view id) {
        const auto it = std::ranges::find(All, id, &ActionDesc::Id);
        return it == All.end() ? nullptr : &*it;
    }
}
