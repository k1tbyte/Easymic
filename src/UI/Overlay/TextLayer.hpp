#pragma once

#include <memory>
#include <string>
#include <gdiplus.h>

#include "Core/Overlay.hpp"
#include "Gdi.hpp"

/// The overlay's own pill: what an action says when it fires. UI thread only.
namespace TextLayer {

    inline std::wstring Text;

    namespace Detail {
        /// Measuring runs GDI+ text layout: redo it only when the text or the font changes.
        struct Prepared {
            std::wstring Text;
            std::wstring Family;
            float Size = 0.0f;
            std::unique_ptr<Gdiplus::Font> Font;
            Gdi::TextMetrics Metrics{};
        };

        inline Prepared Cache;

        inline void Prepare(const OverlayCell& cell) {
            if (Cache.Font && Cache.Size == cell.FontSize && Cache.Text == Text && Cache.Family == cell.FontFamily) {
                return;
            }
            Cache.Text = Text;
            Cache.Family = cell.FontFamily;
            Cache.Size = cell.FontSize;
            Cache.Font.reset(new Gdiplus::Font(Gdi::TextFont(cell.FontSize, cell.FontFamily)));
            Cache.Metrics = Gdi::MeasureText(Text, *Cache.Font);
        }
    }

    inline OverlaySlot Measure(const OverlayCell& cell) {
        if (Text.empty()) {
            Detail::Cache.Font.reset();
            return {};
        }

        Detail::Prepare(cell);
        return {.Width = Detail::Cache.Metrics.Width + cell.Padding * 2};
    }

    inline void Render(const OverlayCell& cell, Gdiplus::Graphics& canvas, const int width) {
        Gdi::DrawCentred(canvas, Text, *Detail::Cache.Font,
                         Gdiplus::RectF(0, Detail::Cache.Metrics.CenterOffset, static_cast<Gdiplus::REAL>(width),
                                        static_cast<Gdiplus::REAL>(cell.Height)));
    }
}
