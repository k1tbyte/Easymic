#pragma once

#include <algorithm>
#include <array>
#include <windows.h>
#include <gdiplus.h>

#include "Core/Overlay.hpp"
#include "Gdi.hpp"

/**
 * @brief Where every layer sits right now, and what the pill around it looks like.
 *
 * The single description of that geometry: the layout pass sizes the window from it and the
 * render pass draws from it, so a measured width and a drawn glyph can never disagree. Indexed
 * by the layer's position in Overlay::Layers, which never changes after startup.
 */
struct OverlaySlots {
    static constexpr float CornerRadius = 10.0f;
    /// Between two pills, so they read as separate things rather than one wide one.
    static constexpr int PillGap = 8;

    // Text size and side padding are fractions of the pill height so both scale with the
    // overlay. The floors stop a small overlay from scaling itself into illegible text wrapped
    // in padding that costs more width than the glyphs do.
    static constexpr float FontScale = 0.46f;
    static constexpr float MinFontSize = 11.0f;
    static constexpr float MaxFontSize = 24.0f;
    static constexpr float PaddingScale = 0.25f;
    static constexpr int MinPadding = 6;

    // Pill background (RGBA)
    static constexpr BYTE BgR = 24;
    static constexpr BYTE BgG = 27;
    static constexpr BYTE BgB = 40;
    static constexpr BYTE BgAlpha = 220;

    /// A pill is the icon with the same amount of padding all round it.
    static constexpr int PillHeight(const int iconSize) { return iconSize * 2; }

    struct Placed {
        OverlaySlot Slot;
        int X = 0;
    };

    int Height = 0;
    /// 0 = nothing to show, so the window has no business being on screen.
    int TotalWidth = 0;
    OverlayCell Cell{};
    std::array<Placed, Overlay::MaxLayers> Items{};
    size_t Count = 0;

    /**
     * @brief Asks every layer how much room it wants, and lays the strip out left to right.
     *
     * Two rules the layers do not get a say in: a pill is never narrower than it is tall, and
     * the gap between two of them is the same everywhere. Both are what stops a pill a feature
     * contributed from looking like a different app.
     */
    static OverlaySlots Measure(const int iconSize, const bool preview) {
        OverlaySlots slots;
        slots.Height = PillHeight(iconSize);

        const auto pillHeight = static_cast<float>(slots.Height);
        slots.Cell = {.Height = slots.Height,
                      .IconSize = iconSize,
                      .FontSize = std::clamp(pillHeight * FontScale, MinFontSize, MaxFontSize),
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

    /// Draws what Measure already decided - no geometry is settled here, which is what keeps the
    /// window we sized and the glyphs we paint from ever disagreeing.
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

            // The layer draws from 0,0 - it asked for a width, not for a place on the strip
            const Gdiplus::GraphicsState state = canvas.Save();
            canvas.TranslateTransform(static_cast<Gdiplus::REAL>(x), 0);
            Overlay::Layers[i].Render(Cell, canvas, slot.Width);
            canvas.Restore(state);
        }
    }
};
