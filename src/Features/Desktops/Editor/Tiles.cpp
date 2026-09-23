#include "Tiles.hpp"

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <vector>

namespace {

    bool _overlap(const LONG lowA, const LONG highA, const LONG lowB, const LONG highB) {
        return lowA < highB && lowB < highA;
    }

    bool _sameRows(const RECT& a, const RECT& b) {
        return _overlap(a.top, a.bottom, b.top, b.bottom);
    }

    bool _sameColumns(const RECT& a, const RECT& b) {
        return _overlap(a.left, a.right, b.left, b.right);
    }

    /// The shift to the nearest guide within snap; none when no guide is that close.
    std::optional<LONG> _snapBy(const LONG value, const std::span<const LONG> guides, const int snap) {
        std::optional<LONG> best;
        for (const LONG guide : guides) {
            const LONG shift = guide - value;
            if (std::abs(shift) <= snap && (!best || std::abs(shift) < std::abs(*best))) {
                best = shift;
            }
        }
        return best;
    }

    LONG _snap(const LONG value, const std::span<const LONG> guides, const int snap) {
        return value + _snapBy(value, guides, snap).value_or(0);
    }

    /// The smaller of the shifts that put either edge of a moving span on a guide.
    LONG _snapShift(const LONG low, const LONG high, const std::span<const LONG> guides, const int snap) {
        const auto byLow = _snapBy(low, guides, snap);
        const auto byHigh = _snapBy(high, guides, snap);
        if (!byLow || !byHigh) {
            return byLow.value_or(byHigh.value_or(0));
        }
        return std::abs(*byLow) <= std::abs(*byHigh) ? *byLow : *byHigh;
    }

    /// How far rect travels by dx before it runs into an obstacle in its rows.
    LONG _sweepX(const RECT& rect, LONG dx, const std::vector<RECT>& obstacles) {
        for (const RECT& obstacle : obstacles) {
            if (!_sameRows(rect, obstacle)) {
                continue;
            }
            if (dx > 0 && obstacle.left >= rect.right) {
                dx = std::min(dx, obstacle.left - rect.right);
            } else if (dx < 0 && obstacle.right <= rect.left) {
                dx = std::max(dx, obstacle.right - rect.left);
            }
        }
        return dx;
    }

    LONG _sweepY(const RECT& rect, LONG dy, const std::vector<RECT>& obstacles) {
        for (const RECT& obstacle : obstacles) {
            if (!_sameColumns(rect, obstacle)) {
                continue;
            }
            if (dy > 0 && obstacle.top >= rect.bottom) {
                dy = std::min(dy, obstacle.top - rect.bottom);
            } else if (dy < 0 && obstacle.bottom <= rect.top) {
                dy = std::max(dy, obstacle.bottom - rect.top);
            }
        }
        return dy;
    }

    RECT _move(const RECT& start, const POINT delta, const Tiles::Limits& limits, const std::vector<RECT>& obstacles) {
        LONG dx = delta.x + _snapShift(start.left + delta.x, start.right + delta.x, limits.GuidesX, limits.Snap);
        LONG dy = delta.y + _snapShift(start.top + delta.y, start.bottom + delta.y, limits.GuidesY, limits.Snap);
        dx = std::max(limits.Bounds.left - start.left, std::min(dx, limits.Bounds.right - start.right));
        dy = std::max(limits.Bounds.top - start.top, std::min(dy, limits.Bounds.bottom - start.bottom));

        // Both axis orders: sliding along a neighbour works one way round and not the other
        const auto sweep = [&](const bool horizontalFirst) {
            RECT rect = start;
            if (horizontalFirst) {
                OffsetRect(&rect, _sweepX(rect, dx, obstacles), 0);
                OffsetRect(&rect, 0, _sweepY(rect, dy, obstacles));
            } else {
                OffsetRect(&rect, 0, _sweepY(rect, dy, obstacles));
                OffsetRect(&rect, _sweepX(rect, dx, obstacles), 0);
            }
            return rect;
        };
        const auto miss = [&](const RECT& rect) {
            return std::abs(rect.left - start.left - dx) + std::abs(rect.top - start.top - dy);
        };
        const RECT first = sweep(true);
        const RECT second = sweep(false);
        return miss(first) <= miss(second) ? first : second;
    }

    RECT _resize(const RECT& start, const unsigned grip, const POINT delta, const Tiles::Limits& limits,
                 const std::vector<RECT>& obstacles) {
        // Whatever stops an edge wins over the minimum size - a small tile never overlaps
        const auto fitX = [&](RECT& rect) {
            if (grip & Tiles::Left) {
                LONG low = limits.Bounds.left;
                for (const RECT& obstacle : obstacles) {
                    if (_sameRows(rect, obstacle) && obstacle.right <= start.left) {
                        low = std::max(low, obstacle.right);
                    }
                }
                rect.left = std::max(low, std::min(rect.left, rect.right - limits.MinSize));
            }
            if (grip & Tiles::Right) {
                LONG high = limits.Bounds.right;
                for (const RECT& obstacle : obstacles) {
                    if (_sameRows(rect, obstacle) && obstacle.left >= start.right) {
                        high = std::min(high, obstacle.left);
                    }
                }
                rect.right = std::min(high, std::max(rect.right, rect.left + limits.MinSize));
            }
        };
        const auto fitY = [&](RECT& rect) {
            if (grip & Tiles::Top) {
                LONG low = limits.Bounds.top;
                for (const RECT& obstacle : obstacles) {
                    if (_sameColumns(rect, obstacle) && obstacle.bottom <= start.top) {
                        low = std::max(low, obstacle.bottom);
                    }
                }
                rect.top = std::max(low, std::min(rect.top, rect.bottom - limits.MinSize));
            }
            if (grip & Tiles::Bottom) {
                LONG high = limits.Bounds.bottom;
                for (const RECT& obstacle : obstacles) {
                    if (_sameColumns(rect, obstacle) && obstacle.top >= start.bottom) {
                        high = std::min(high, obstacle.top);
                    }
                }
                rect.bottom = std::min(high, std::max(rect.bottom, rect.top + limits.MinSize));
            }
        };

        RECT rect = start;
        if (grip & Tiles::Left) rect.left = _snap(start.left + delta.x, limits.GuidesX, limits.Snap);
        if (grip & Tiles::Right) rect.right = _snap(start.right + delta.x, limits.GuidesX, limits.Snap);
        fitX(rect);
        if (grip & Tiles::Top) rect.top = _snap(start.top + delta.y, limits.GuidesY, limits.Snap);
        if (grip & Tiles::Bottom) rect.bottom = _snap(start.bottom + delta.y, limits.GuidesY, limits.Snap);
        fitY(rect);
        // The rows may have grown into an obstacle's, which the first pass did not see
        fitX(rect);
        return rect;
    }

} // anonymous namespace

namespace Tiles {

    unsigned HitTest(const RECT& tile, const POINT point, const int border) {
        if (!PtInRect(&tile, point)) {
            return None;
        }
        unsigned grip = None;
        if (point.x < tile.left + border) {
            grip |= Left;
        } else if (point.x >= tile.right - border) {
            grip |= Right;
        }
        if (point.y < tile.top + border) {
            grip |= Top;
        } else if (point.y >= tile.bottom - border) {
            grip |= Bottom;
        }
        return grip ? grip : Body;
    }

    RECT Drag(const RECT& start, const unsigned grip, const POINT delta, const Limits& limits) {
        std::vector<RECT> obstacles;
        for (const RECT& other : limits.Obstacles) {
            if (!_sameRows(start, other) || !_sameColumns(start, other)) {
                obstacles.push_back(other);
            }
        }
        return grip == Body ? _move(start, delta, limits, obstacles) : _resize(start, grip, delta, limits, obstacles);
    }
}
