#include "Preview.hpp"

#include "AppConfig.hpp"
#include "Painter.hpp"

#include <algorithm>

namespace {

    BOOL CALLBACK _collectMonitor(HMONITOR, HDC, LPRECT rect, const LPARAM param) {
        reinterpret_cast<std::vector<RECT>*>(param)->push_back(*rect);
        return TRUE;
    }

} // anonymous namespace

namespace Preview {

    void Paint(const HDC dc, const RECT& area, const std::vector<WindowRule>& windows, const float textSize) {
        const int width = area.right - area.left;
        const int height = area.bottom - area.top;
        if (width <= 0 || height <= 0) {
            return;
        }

        // Off screen first, or every repaint flashes the background
        Gdiplus::Bitmap buffer(width, height, PixelFormat32bppPARGB);
        Gdiplus::Graphics canvas(&buffer);
        canvas.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        canvas.Clear(Gdiplus::Color(255, 32, 32, 36));

        const RECT screen{GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                          GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
                          GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
        const float scale = std::min(static_cast<float>(width) / (screen.right - screen.left),
                                     static_cast<float>(height) / (screen.bottom - screen.top));
        const float originX = (width - (screen.right - screen.left) * scale) / 2 - screen.left * scale;
        const float originY = (height - (screen.bottom - screen.top) * scale) / 2 - screen.top * scale;
        const auto map = [&](const RECT& rect) {
            return Gdiplus::RectF(originX + rect.left * scale, originY + rect.top * scale,
                                  (rect.right - rect.left) * scale, (rect.bottom - rect.top) * scale);
        };

        std::vector<RECT> monitors;
        EnumDisplayMonitors(nullptr, nullptr, _collectMonitor, reinterpret_cast<LPARAM>(&monitors));
        std::vector<Gdiplus::RectF> mapped;
        for (const RECT& monitor : monitors) {
            mapped.push_back(map(monitor));
        }
        Painter::Monitors(canvas, mapped);
        Painter::Tiles(canvas, Painter::FromRules(windows), originX, originY, scale, textSize);

        Gdiplus::Graphics(dc).DrawImage(&buffer, static_cast<INT>(area.left), static_cast<INT>(area.top));
    }
}
