#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>
#include <span>
#include <string_view>

/**
 * @brief The one owner of the low-level hooks, and the pipeline every consumer of input sits in.
 *
 * A consumer is a stage. Events walk the enabled stages in Order on the input thread, and each
 * stage answers at once - anything slow is handed off. Input a stage sends is seen only by the
 * stages after it, so a remap can feed hotkeys and nothing loops. The hooks are up only while an
 * enabled stage wants them. docs/INPUT.md holds the rules.
 */
namespace Input {

    /// What a stage wants to be shown. A mouse button reaches OnKey as its VK_*BUTTON code.
    using Wants = uint8_t;
    inline constexpr Wants WantKeys = 1;
    inline constexpr Wants WantButtons = 2;

    enum class Verdict : uint8_t {
        /// On to the later stages.
        Next,
        /// To the app, skipping the later stages.
        Deliver,
        /// Nobody else sees it. A consumed down takes its repeats and its up with it.
        Consume,
    };

    struct KeyEvent {
        uint8_t Vk;
        bool Down;
        /// A down while this stage already has the key down.
        bool Repeat;
    };

    struct Stage {
        std::string_view Id;
        /// Who sees an event first: 0 capture, 100 remap, 200 hotkeys, 300 typed text.
        int Order;
        /// Input thread, inside the hook: O(1), no allocation, no waiting. A stage never gets an up
        /// without its down, and the verdict of an up counts only as Deliver - an app must not be
        /// left holding a key it saw go down.
        Verdict (*OnKey)(const KeyEvent&) = nullptr;
        /// Input thread. What the stage knew about held keys is void: it was just enabled, or the
        /// hooks or the desktop changed under it.
        void (*OnReset)() = nullptr;
    };

    /// Before Start - the pipeline is fixed once the thread runs.
    void Add(const Stage& stage);

    bool Start();
    /// Joins the thread. Hooks go down with it.
    void Stop();

    /// Any thread. Enabling again replaces what the stage wants and starts it over.
    void Enable(std::string_view id, Wants wants);
    void Disable(std::string_view id);

    /// Runs the work on the input thread, in order with Enable and Disable. Dropped when the
    /// thread is not running.
    void Post(std::move_only_function<void()> work);

    /// Any thread. Tags the events with the sender's level so only the stages after it see them.
    /// @return how many events SendInput took.
    UINT Send(std::span<INPUT> inputs, std::string_view from);
}
