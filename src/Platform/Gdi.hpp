#pragma once

#include <cmath>
#include <string>
#include <windows.h>
#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")

/// Platform rather than UI on purpose - whatever paints needs these, and a feature that draws
/// into the overlay must not have to include a window header to get them.
namespace Gdi {

    inline void RoundedRect(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rect, const float radius) {
        const float diameter = radius * 2.0f;

        path.AddArc(rect.X, rect.Y, diameter, diameter, 180, 90);
        path.AddArc(rect.X + rect.Width - diameter, rect.Y, diameter, diameter, 270, 90);
        path.AddArc(rect.X + rect.Width - diameter, rect.Y + rect.Height - diameter, diameter, diameter, 0, 90);
        path.AddArc(rect.X, rect.Y + rect.Height - diameter, diameter, diameter, 90, 90);

        path.CloseFigure();
    }

    inline Gdiplus::Font TextFont(const float size, const wchar_t* family = L"Segoe UI") {
        return {family, size, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel};
    }

    struct TextMetrics {
        int Width;
        float CenterOffset;
    };

    /// MeasureString centers the line box; glyph outlines locate the visible centre.
    inline TextMetrics MeasureText(const std::wstring& text, const Gdiplus::Font& font) {
        HDC screenDC = GetDC(nullptr);
        Gdiplus::RectF bounds;
        Gdiplus::RectF ink;

        { // the Graphics has to go before the DC it was built on
            Gdiplus::Graphics graphics(screenDC);
            Gdiplus::StringFormat format;
            // Without this GDI+ drops trailing whitespace from the measurement but still draws it
            format.SetFormatFlags(Gdiplus::StringFormatFlagsMeasureTrailingSpaces);

            graphics.MeasureString(text.c_str(), -1, &font, Gdiplus::PointF(0, 0), &format, &bounds);
            Gdiplus::FontFamily family;
            font.GetFamily(&family);
            Gdiplus::GraphicsPath path;
            path.AddString(text.c_str(), -1, &family, font.GetStyle(), font.GetSize(),
                           Gdiplus::PointF(0, 0), &format);
            path.GetBounds(&ink);
        }

        ReleaseDC(nullptr, screenDC);
        return {static_cast<int>(std::ceil(bounds.Width)),
                bounds.Height / 2.0f - ink.Y - ink.Height / 2.0f};
    }

    /// Off-white rather than white by default: on the overlay's dark pill, pure white on a
    /// bright glyph next to it reads as two different whites.
    inline void DrawCentred(Gdiplus::Graphics& canvas, const std::wstring& text, const Gdiplus::Font& font,
                            const Gdiplus::RectF& rect,
                            const Gdiplus::Color& colour = Gdiplus::Color(255, 240, 240, 240)) {
        Gdiplus::StringFormat format;
        format.SetAlignment(Gdiplus::StringAlignmentCenter);
        format.SetLineAlignment(Gdiplus::StringAlignmentCenter);

        const Gdiplus::SolidBrush brush(colour);
        canvas.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
        canvas.DrawString(text.c_str(), -1, &font, rect, &format, &brush);
    }
}
