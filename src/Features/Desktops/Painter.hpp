#pragma once

#include <span>
#include <string>
#include <vector>
#include <windows.h>
#include <gdiplus.h>

struct WindowRule;

namespace Painter {

    struct Tile {
        /// Physical pixels of the virtual screen.
        RECT Screen;
        std::wstring Name;
        size_t Rule = 0;
        /// Shown as its monitor's work area; the editor cannot move it.
        bool Maximized = false;
        bool Active = false;
    };

    std::vector<Tile> FromRules(const std::vector<WindowRule>& windows);

    void Monitors(Gdiplus::Graphics& canvas, std::span<const Gdiplus::RectF> monitors);

    /// Back to front, so the first tile wins.
    void Tiles(Gdiplus::Graphics& canvas, std::span<const Tile> tiles,
               float originX, float originY, float scale, float textSize);
}
