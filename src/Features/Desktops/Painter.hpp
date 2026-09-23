#pragma once

#include <span>
#include <string>
#include <vector>
#include <windows.h>
#include <gdiplus.h>

struct WindowRule;

/// How the preview and the tile editor draw a window rule: a translucent tile named after its app.
namespace Painter {

    struct Tile {
        /// Physical pixels of the virtual screen.
        RECT Screen;
        std::wstring Name;
        /// Which rule the tile is, so an edit lands back on it.
        size_t Rule = 0;
        /// Shown as its monitor's work area; the editor's tiles it cannot move.
        bool Maximized = false;
        bool Active = false;
    };

    /// One tile per rule with a size, in the order given.
    std::vector<Tile> FromRules(const std::vector<WindowRule>& windows);

    void Monitors(Gdiplus::Graphics& canvas, std::span<const Gdiplus::RectF> monitors);

    /// A tile's screen rect lands at origin + rect * scale. Back to front, so the first tile wins.
    void Tiles(Gdiplus::Graphics& canvas, std::span<const Tile> tiles,
               float originX, float originY, float scale, float textSize);
}
