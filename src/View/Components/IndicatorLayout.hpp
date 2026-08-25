#ifndef EASYMIC_INDICATORLAYOUT_HPP
#define EASYMIC_INDICATORLAYOUT_HPP

#include <algorithm>
#include <cmath>
#include <string>
#include <windows.h>
#include <gdiplus.h>

/**
 * @brief Where the indicator draws: the mic pill, the notification pill next to it, or either alone.
 *
 * The single description of that geometry. The layout pass sizes the window from it and the render
 * pass draws from it, so a measured width and a drawn glyph can never disagree.
 */
struct IndicatorLayout {
    int height = 0;
    int totalWidth = 0;
    int iconSize = 0;
    int textX = 0;
    int textWidth = 0;
    float fontSize = 0.0f;
    bool hasMic = false;
    bool hasText = false;

    static constexpr float CornerRadius = 10.0f;
    static constexpr int PillGap = 8;

    /// The icon is drawn at IndicatorSize with the same amount of padding around it.
    static constexpr int PillSize(const int indicatorSize) { return indicatorSize * 2; }

    Gdiplus::Font MakeFont() const {
        return {L"Segoe UI", fontSize, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel};
    }

    static IndicatorLayout Compute(const int indicatorSize, const bool micVisible, const std::wstring& text) {
        IndicatorLayout layout;
        layout.iconSize = indicatorSize;
        layout.height = PillSize(indicatorSize);
        layout.hasMic = micVisible;
        layout.hasText = !text.empty();
        // An indicator with nothing to draw still holds the mic pill's footprint: "muted or talking"
        // keeps an empty window on screen so the peak timer has somewhere to live
        layout.totalWidth = layout.height;

        if (!layout.hasText) {
            return layout;
        }

        layout.fontSize = std::clamp(static_cast<float>(layout.height) * 0.40f, 10.0f, 24.0f);
        layout.textX = micVisible ? layout.height + PillGap : 0;

        const int padding = std::max(10, layout.height / 3);
        layout.textWidth = std::max(layout.height, MeasureText(text, layout.MakeFont()) + padding * 2);
        layout.totalWidth = layout.textX + layout.textWidth;
        return layout;
    }

private:
    static int MeasureText(const std::wstring& text, const Gdiplus::Font& font) {
        HDC screenDC = GetDC(nullptr);
        Gdiplus::Graphics graphics(screenDC);

        Gdiplus::StringFormat format;
        // Without this GDI+ drops trailing whitespace from the measurement but still draws it
        format.SetFormatFlags(Gdiplus::StringFormatFlagsMeasureTrailingSpaces);

        Gdiplus::RectF bounds;
        graphics.MeasureString(text.c_str(), -1, &font, Gdiplus::PointF(0, 0), &format, &bounds);

        ReleaseDC(nullptr, screenDC);
        return static_cast<int>(std::ceil(bounds.Width));
    }
};

#endif //EASYMIC_INDICATORLAYOUT_HPP
