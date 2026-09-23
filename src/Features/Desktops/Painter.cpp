#include "Painter.hpp"

#include "AppConfig.hpp"
#include "Gdi.hpp"
#include "Str.hpp"

namespace {

    constexpr COLORREF Palette[] = {
        RGB(66, 133, 244), RGB(52, 168, 83), RGB(244, 160, 0),
        RGB(219, 68, 55),  RGB(156, 39, 176), RGB(0, 172, 193),
    };

} // anonymous namespace

namespace Painter {

    std::vector<Tile> FromRules(const std::vector<WindowRule>& windows) {
        std::vector<Tile> tiles;
        for (size_t i = 0; i < windows.size(); ++i) {
            const WindowRule& rule = windows[i];
            RECT rect{rule.X, rule.Y, rule.X + rule.Width, rule.Y + rule.Height};
            if (IsRectEmpty(&rect)) {
                continue;
            }
            if (rule.Maximized) {
                MONITORINFO info{.cbSize = sizeof(info)};
                GetMonitorInfoW(MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST), &info);
                rect = info.rcWork;
            }

            std::wstring name = Str::Utf8ToWide(rule.Exe);
            if (name.ends_with(L".exe")) {
                name.resize(name.size() - 4);
            }
            tiles.push_back({rect, std::move(name), i, rule.Maximized});
        }
        return tiles;
    }

    void Monitors(Gdiplus::Graphics& canvas, const std::span<const Gdiplus::RectF> monitors) {
        const Gdiplus::SolidBrush desk(Gdiplus::Color(255, 58, 58, 64));
        const Gdiplus::Pen bezel(Gdiplus::Color(255, 110, 110, 118));
        for (const Gdiplus::RectF& monitor : monitors) {
            canvas.FillRectangle(&desk, monitor);
            canvas.DrawRectangle(&bezel, monitor);
        }
    }

    void Tiles(Gdiplus::Graphics& canvas, const std::span<const Tile> tiles,
               const float originX, const float originY, const float scale, const float textSize) {
        const Gdiplus::Font font = Gdi::TextFont(textSize);
        for (size_t i = tiles.size(); i-- > 0;) {
            const Tile& tile = tiles[i];
            const COLORREF colour = Palette[i % std::size(Palette)];
            const BYTE alpha = tile.Maximized ? 80 : tile.Active ? 220 : 170;
            const Gdiplus::SolidBrush fill(Gdiplus::Color(alpha, GetRValue(colour), GetGValue(colour), GetBValue(colour)));
            const Gdiplus::Pen border(Gdiplus::Color(255, GetRValue(colour), GetGValue(colour), GetBValue(colour)),
                                      tile.Active ? 2.f : 1.f);
            const Gdiplus::RectF at(originX + tile.Screen.left * scale, originY + tile.Screen.top * scale,
                                    (tile.Screen.right - tile.Screen.left) * scale,
                                    (tile.Screen.bottom - tile.Screen.top) * scale);
            canvas.FillRectangle(&fill, at);
            canvas.DrawRectangle(&border, at);
            Gdi::DrawCentred(canvas, tile.Name, font, at);
        }
    }
}
