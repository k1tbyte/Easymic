#pragma once

#include <string>
#include <gdiplus.h>

#include "Core/Overlay.hpp"
#include "Gdi.hpp"

/**
 * @brief The overlay's own pill: what an action says when it fires.
 *
 * Not a feature's layer, deliberately - the text arrives through Feedback from whichever action
 * raised it, and showing a line of text is the one thing the overlay knows how to do by itself.
 * UI thread only: the worker hands the text over through the window's message queue.
 */
namespace TextLayer {

    inline std::wstring Text;

    inline OverlaySlot Measure(const OverlayCell& cell) {
        if (Text.empty()) {
            return {};
        }

        return {.Width = Gdi::MeasureText(Text, Gdi::TextFont(cell.FontSize)) + cell.Padding * 2};
    }

    inline void Render(const OverlayCell& cell, Gdiplus::Graphics& canvas, const int width) {
        Gdi::DrawCentred(canvas, Text, Gdi::TextFont(cell.FontSize),
                         Gdiplus::RectF(0, 0, static_cast<Gdiplus::REAL>(width),
                                        static_cast<Gdiplus::REAL>(cell.Height)));
    }
}
