#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>
#include <span>
#include <string_view>

namespace Input {

    using Wants = uint8_t;
    inline constexpr Wants WantKeys = 1;
    inline constexpr Wants WantButtons = 2;

    enum class Verdict : uint8_t {
        Next,
        Deliver,
        /// A consumed down takes its repeats and its up with it.
        Consume,
    };

    using HoldId = uint32_t;
    using Work = std::move_only_function<void()>;

    struct KeyEvent {
        uint8_t Vk;
        bool Down;
        bool Repeat;
    };

    struct Stage {
        std::string_view Id;
        int Order;
        /// Input thread, inside the hook: O(1). The verdict of an up counts only as Deliver.
        Verdict (*OnKey)(const KeyEvent&) = nullptr;
        void (*OnReset)() = nullptr;
        void (*OnHold)(HoldId) = nullptr;
    };

    void Add(const Stage& stage);

    bool Start();
    void Stop();

    /// Enable, Disable, Post, Send and Edit: any thread. Post runs in order with Enable and Disable.
    void Enable(std::string_view id, Wants wants);
    void Disable(std::string_view id);
    void Post(Work work);
    UINT Send(std::span<INPUT> inputs, std::string_view from);

    /// work runs on the threadpool under a hold and Posts its Commit; landed runs on the input
    /// thread once an edit was sent.
    void Edit(Work work, Work landed = {});
    HoldId CurrentHold();
    /// Input thread. False once the hold expired and went out unedited.
    bool Commit(HoldId id, std::span<const INPUT> edit);
}
