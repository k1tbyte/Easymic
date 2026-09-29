#pragma once

#include <string>

#include "Core/Overlay.hpp"
#include "Gfx/Gdi.hpp"

/// The overlay's own pill: what an action says when it fires. UI thread only.
namespace TextLayer {

    inline std::wstring Text;

    namespace Detail {
        inline Gdi::TextLayout Layout;
    }

    inline OverlaySlot Measure(const OverlayCell& cell) {
        if (Text.empty()) {
            Detail::Layout.Reset();
            return {};
        }

        const auto metrics = Detail::Layout.Measure(Text, cell.FontSize, cell.FontFamily);
        return {.Width = metrics.Width + cell.Padding * 2};
    }

    inline void Render(const OverlayCell& cell, Gdiplus::Graphics& canvas, const int width) {
        Detail::Layout.Draw(canvas, width, cell.Height);
    }
}
