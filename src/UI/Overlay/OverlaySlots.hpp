#pragma once

#include <algorithm>
#include <array>
#include <windows.h>
#include <gdiplus.h>

#include "Core/Overlay.hpp"
#include "Gfx/Gdi.hpp"

/// The one description of the strip's geometry, so a measured width and a drawn glyph cannot disagree.
struct OverlaySlots {
    static constexpr float CornerRadius = 10.0f;
    static constexpr int PillGap = 8;

    // Fractions of the pill height; the floors keep a small overlay legible.
    static constexpr float FontScale = 0.46f;
    static constexpr float MinFontSize = 11.0f;
    static constexpr float MaxFontSize = 24.0f;
    static constexpr float PaddingScale = 0.25f;
    static constexpr int MinPadding = 6;

    static constexpr BYTE BgR = 24;
    static constexpr BYTE BgG = 27;
    static constexpr BYTE BgB = 40;
    static constexpr BYTE BgAlpha = 220;

    static constexpr int PillHeight(const int iconSize) { return iconSize * 2; }

    struct Placed {
        OverlaySlot Slot;
        int X = 0;
    };

    int Height = 0;
    int TotalWidth = 0;
    OverlayCell Cell{};
    std::array<Placed, Overlay::MaxLayers> Items{};
    size_t Count = 0;

    /// A pill is never narrower than it is tall and the gap is uniform: layers get no say, so a
    /// feature's pill cannot look like another app's.
    static OverlaySlots Measure(const int iconSize, const bool preview, const wchar_t* fontFamily = L"Segoe UI") {
        OverlaySlots slots;
        slots.Height = PillHeight(iconSize);

        const auto pillHeight = static_cast<float>(slots.Height);
        slots.Cell = {.Height = slots.Height,
                      .IconSize = iconSize,
                      .FontSize = std::clamp(pillHeight * FontScale, MinFontSize, MaxFontSize),
                      .FontFamily = fontFamily,
                      .Padding = std::max(MinPadding, static_cast<int>(pillHeight * PaddingScale)),
                      .Preview = preview};

        slots.Count = std::min(Overlay::Layers.size(), Overlay::MaxLayers);

        for (size_t i = 0; i < slots.Count; i++) {
            OverlaySlot slot = Overlay::Layers[i].Measure(slots.Cell);
            if (slot.Width > 0) {
                slot.Width = std::max(slot.Width, slots.Height);
                slots.Items[i].X = slots.TotalWidth ? slots.TotalWidth + PillGap : 0;
                slots.TotalWidth = slots.Items[i].X + slot.Width;
            }
            slots.Items[i].Slot = slot;
        }

        return slots;
    }

    void Render(Gdiplus::Graphics& canvas) const {
        const Gdiplus::SolidBrush background(Gdiplus::Color(BgAlpha, BgR, BgG, BgB));

        for (size_t i = 0; i < Count; i++) {
            const auto& [slot, x] = Items[i];
            if (slot.Width <= 0) {
                continue;
            }

            Gdiplus::GraphicsPath pill;
            Gdi::RoundedRect(pill, Gdiplus::RectF(static_cast<float>(x), 0,
                                                  static_cast<float>(slot.Width),
                                                  static_cast<float>(Height)),
                             CornerRadius);
            canvas.FillPath(&background, &pill);

            const Gdiplus::GraphicsState state = canvas.Save();
            canvas.TranslateTransform(static_cast<Gdiplus::REAL>(x), 0);
            Overlay::Layers[i].Render(Cell, canvas, slot.Width);
            canvas.Restore(state);
        }
    }
};
