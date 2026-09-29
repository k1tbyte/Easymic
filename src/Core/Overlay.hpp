#pragma once

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>
#include <windows.h>
#include <gdiplus.h>

/// The geometry a layer scales everything from, before it has been given a slot.
struct OverlayCell {
    /// The pill height - the one size everything else is a fraction of.
    int Height = 0;
    int IconSize = 0;
    float FontSize = 0.0f;
    const wchar_t* FontFamily = L"Segoe UI";
    /// Side padding a text pill leaves around its glyphs. Handed over rather than derived, so
    /// two layers that draw text cannot disagree about it.
    int Padding = 0;
    /// The settings window is open, so show a state rather than nothing - the user is dragging
    /// the overlay around and has to see what they are dragging.
    bool Preview = false;
};

struct OverlaySlot {
    /// 0 = nothing on screen right now.
    int Width = 0;
    /// Poll me anyway. At width 0 this is how a layer watches for its own cue without keeping an
    /// empty window on screen to host a timer.
    bool WantsTick = false;
};

/**
 * @brief One thing drawn on the overlay, contributed by whoever the content belongs to.
 *
 * Plain function pointers, like ActionDesc::Make and SettingsPage::Build: the descriptor stays
 * POD and costs nothing in binary size. A layer therefore cannot capture - it keeps its state in
 * its own slice and reads it on the UI thread, which is the only thread that calls any of these.
 */
struct OverlayLayer {
    std::string_view Id;
    /// Left to right. Overlay::Last pins the overlay's own text pill after everything else.
    int Order = 100;
    OverlaySlot (*Measure)(const OverlayCell&);
    /// The origin is the slot, so a layer draws from 0,0 across the width Measure asked for.
    void (*Render)(const OverlayCell&, Gdiplus::Graphics& canvas, int width);
    /// Tick interval, 0 for a layer that has nothing to poll.
    uint16_t TickMs = 0;
    /// Invalidates by itself when it changed something.
    void (*Tick)() = nullptr;
};

namespace Overlay {

    /// A layer leaves Order alone unless it has a reason to be last.
    inline constexpr int Last = 1000;

    /// One timer per layer that asks for one, so two layers can poll at different rates without
    /// agreeing on a common interval. Registration happens at startup and nothing is ever
    /// removed, so a layer's index is its identity for the lifetime of the process.
    inline constexpr size_t MaxLayers = 8;

    inline std::vector<OverlayLayer> Layers;

    inline void Add(const OverlayLayer& layer) {
        Layers.insert(std::ranges::upper_bound(Layers, layer.Order, {}, &OverlayLayer::Order), layer);
    }

    /// Set by the frame once its window exists, the way Feedback takes its PostFn - the kernel
    /// has no business knowing which window draws. Callable from any thread: the UI side posts.
    inline void (*Invalidate)() = nullptr;

    /// What a layer calls when it has something new to show. Safe before the surface exists,
    /// which is what a module registering in main needs.
    inline void Changed() {
        if (Invalidate) {
            Invalidate();
        }
    }
}
