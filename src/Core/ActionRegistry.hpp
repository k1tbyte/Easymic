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
    /// Fires on press and release by design (push to talk), so "trigger on release" is meaningless.
    HoldOnly = 1u << 0,
    /// The action needs no sound of its own - muting already has its own feedback.
    NoSound = 1u << 1,
    /// The argument is a command line, so the command tokens apply to it and the dialog offers a
    /// command row rather than a plain text one.
    RunsCommand = 1u << 2,
};

constexpr ActionFlags operator|(const ActionFlags a, const ActionFlags b) {
    return static_cast<ActionFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr bool HasFlag(const ActionFlags value, const ActionFlags flag) {
    return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
}

/// What the factory below is given. Args is a reference into the config, so a factory that needs
/// it past the call has to copy it - the config is rewritten whenever settings are applied.
struct ActionContext {
    const std::string& Args;
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
    ActionFn (*Make)(const ActionContext&) = nullptr;
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
