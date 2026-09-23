#pragma once

#include <span>
#include <windows.h>

/// The tile editor's geometry, with nothing drawn and no window: what a press grabs, where a drag lands.
namespace Tiles {

    enum Grip : unsigned {
        None = 0,
        Left = 1,
        Top = 2,
        Right = 4,
        Bottom = 8,
        Body = Left | Top | Right | Bottom,
    };

    /// The edges within border of the point, Body further in, None outside.
    unsigned HitTest(const RECT& tile, POINT point, int border);

    struct Limits {
        /// The other tiles. One already overlapping the start rect is no obstacle, so a tile that
        /// began on top of another can be dragged off it.
        std::span<const RECT> Obstacles;
        std::span<const LONG> GuidesX;
        std::span<const LONG> GuidesY;
        RECT Bounds;
        int Snap;
        int MinSize;
    };

    /// start with the gripped edges moved by delta: snapped to the nearest guide, then stopped by
    /// the bounds and the first obstacle in the way. Never overlaps an obstacle.
    RECT Drag(const RECT& start, unsigned grip, POINT delta, const Limits& limits);
}
