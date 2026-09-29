#pragma once

#include <cmath>
#include <memory>
#include <string>
#include <windows.h>
#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")

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
            // Without this GDI+ drops trailing whitespace from the measurement but still draws it.
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

    class TextLayout {
        std::wstring _text;
        std::wstring _family;
        float _size = 0.0f;
        std::unique_ptr<Gdiplus::Font> _font;
        TextMetrics _metrics{};

    public:
        TextMetrics Measure(const std::wstring& text, const float size, const wchar_t* family) {
            const bool fontChanged = !_font || _size != size || _family != family;
            if (!fontChanged && _text == text) {
                return _metrics;
            }
            if (fontChanged) {
                _font.reset(new Gdiplus::Font(TextFont(size, family)));
                _family = family;
                _size = size;
            }
            _text = text;
            _metrics = MeasureText(_text, *_font);
            return _metrics;
        }

        void Draw(Gdiplus::Graphics& canvas, const int width, const int height) const {
            DrawCentred(canvas, _text, *_font,
                        Gdiplus::RectF(0, _metrics.CenterOffset, static_cast<float>(width), static_cast<float>(height)));
        }

        void Reset() { *this = {}; }
    };
}
