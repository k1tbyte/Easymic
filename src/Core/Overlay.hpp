#pragma once

#include <cstdint>
#include <string_view>
#include <vector>
#include <windows.h>

#include "OrderedInsert.hpp"

namespace Gdiplus {
    class Graphics;
}

struct OverlayCell {
    int Height = 0;
    int IconSize = 0;
    float FontSize = 0.0f;
    const wchar_t* FontFamily = L"Segoe UI";
    int Padding = 0;
    /// Settings is open: show a state rather than nothing, so the overlay can be dragged into place.
    bool Preview = false;
};

struct OverlaySlot {
    int Width = 0;
    /// Tick me: the only thing that starts the layer's timer.
    bool WantsTick = false;
};

/// Plain function pointers: a layer cannot capture, so its state lives in its slice, read on the UI thread only.
struct OverlayLayer {
    std::string_view Id;
    int Order = 100;
    OverlaySlot (*Measure)(const OverlayCell&);
    /// The origin is the slot: draw from 0,0 across the width Measure asked for.
    void (*Render)(const OverlayCell&, Gdiplus::Graphics& canvas, int width);
    uint16_t TickMs = 0;
    void (*Tick)() = nullptr;
};

namespace Overlay {

    inline constexpr int Last = 1000;

    inline constexpr size_t MaxLayers = 8;

    inline std::vector<OverlayLayer> Layers;

    inline void Add(const OverlayLayer& layer) {
        InsertByOrder(Layers, layer);
    }

    inline void (*Invalidate)() = nullptr;

    /// Any thread (the UI side posts); safe before the surface exists.
    inline void Changed() {
        if (Invalidate) {
            Invalidate();
        }
    }
}
