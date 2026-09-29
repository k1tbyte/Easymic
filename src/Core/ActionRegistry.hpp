#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

class Feedback;

/// Runs on the Dispatcher worker, never in a hook.
using ActionFn = std::function<void()>;

enum class ActionFlags : uint32_t {
    None = 0,
    NoSound = 1u << 0,
    RunsCommand = 1u << 1,
    /// Press only; the handler runs on the edit lane under a hold (Input::Edit), not on the action worker.
    EditsText = 1u << 2,
};

constexpr ActionFlags operator|(const ActionFlags a, const ActionFlags b) {
    return static_cast<ActionFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr bool HasFlag(const ActionFlags value, const ActionFlags flag) {
    return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
}

/// The references die with the call: copy what the action keeps (the config is rewritten on every Apply).
struct ActionContext {
    const std::string& Args;
    const std::string& Notification;
    Feedback& Fb;
};

struct ActionDesc {
    std::string_view Id;
    std::string_view Title;
    std::string_view Group;
    ActionFlags Flags = ActionFlags::None;
    std::string_view DefaultSound;
    std::string_view DefaultNotification;
    std::string_view ArgsLabel;
    std::string_view ArgsHint;
    /// An empty result declines the binding: it is not registered at all.
    ActionFn (*Make)(const ActionContext&) = nullptr;
    ActionFn (*MakeRelease)(const ActionContext&) = nullptr;
};

namespace ActionRegistry {

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
