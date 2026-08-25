#ifndef EASYMIC_INDICATORLAYOUT_HPP
#define EASYMIC_INDICATORLAYOUT_HPP

#include <algorithm>
#include <cmath>
#include <string>
#include <windows.h>
#include <gdiplus.h>

#include "GdiRenderer.hpp"

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

    // Text size and side padding are fractions of the pill height so both scale with the
    // indicator. The floors stop a small indicator from scaling itself into illegible text
    // wrapped in padding that costs more width than the glyphs do.
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

        const auto pillHeight = static_cast<float>(layout.height);
        layout.fontSize = std::clamp(pillHeight * FontScale, MinFontSize, MaxFontSize);
        layout.textX = micVisible ? layout.height + PillGap : 0;

        const int padding = std::max(MinPadding, static_cast<int>(pillHeight * PaddingScale));
        layout.textWidth = std::max(layout.height, MeasureText(text, layout.MakeFont()) + padding * 2);
        layout.totalWidth = layout.textX + layout.textWidth;
        return layout;
    }

    /// Draws what Compute already measured - no geometry is decided here, which is what keeps
    /// the window we sized and the glyphs we paint from ever disagreeing.
    void Render(const RenderContext& ctx, Gdiplus::Bitmap* micBitmap, const std::wstring& text) const {
        if (!hasMic && !hasText) {
            return;
        }

        const auto pillHeight = static_cast<float>(height);
        Gdiplus::SolidBrush brush(Gdiplus::Color(BgAlpha, BgR, BgG, BgB));

        if (hasMic) {
            Gdiplus::GraphicsPath micPath;
            GDIRenderer::CreateRoundedRectPath(micPath, Gdiplus::RectF(0, 0, pillHeight, pillHeight),
                                               CornerRadius);
            ctx.graphics->FillPath(&brush, &micPath);

            const int inset = (height - iconSize) / 2;
            ctx.graphics->DrawImage(micBitmap, inset, inset, iconSize, iconSize);
        }

        if (!hasText) {
            return;
        }

        const Gdiplus::RectF textRect(static_cast<float>(textX), 0,
                                      static_cast<float>(textWidth), pillHeight);

        Gdiplus::GraphicsPath textPath;
        GDIRenderer::CreateRoundedRectPath(textPath, textRect, CornerRadius);
        ctx.graphics->FillPath(&brush, &textPath);

        Gdiplus::StringFormat format;
        format.SetAlignment(Gdiplus::StringAlignmentCenter);
        format.SetLineAlignment(Gdiplus::StringAlignmentCenter);

        const auto font = MakeFont();
        Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 240, 240, 240));
        ctx.graphics->SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
        ctx.graphics->DrawString(text.c_str(), -1, &font, textRect, &format, &textBrush);
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
