#include "Tiles.hpp"

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <vector>

namespace {

    struct Axis {
        LONG RECT::* Low;
        LONG RECT::* High;
        LONG POINT::* Delta;
        unsigned LowGrip;
        unsigned HighGrip;
        std::span<const LONG> Tiles::Limits::* Guides;
    };

    constexpr Axis Horizontal{&RECT::left, &RECT::right, &POINT::x, Tiles::Left, Tiles::Right, &Tiles::Limits::GuidesX};
    constexpr Axis Vertical{&RECT::top, &RECT::bottom, &POINT::y, Tiles::Top, Tiles::Bottom, &Tiles::Limits::GuidesY};

    const Axis& _across(const Axis& axis) {
        return &axis == &Horizontal ? Vertical : Horizontal;
    }

    bool _overlap(const RECT& a, const RECT& b, const Axis& axis) {
        return a.*axis.Low < b.*axis.High && b.*axis.Low < a.*axis.High;
    }

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

    LONG _snapShift(const LONG low, const LONG high, const std::span<const LONG> guides, const int snap) {
        const auto byLow = _snapBy(low, guides, snap);
        const auto byHigh = _snapBy(high, guides, snap);
        if (!byLow || !byHigh) {
            return byLow.value_or(byHigh.value_or(0));
        }
        return std::abs(*byLow) <= std::abs(*byHigh) ? *byLow : *byHigh;
    }

    /// How far rect travels by `by` along the axis before it runs into an obstacle in its lane.
    LONG _sweep(const RECT& rect, LONG by, const std::vector<RECT>& obstacles, const Axis& axis) {
        for (const RECT& obstacle : obstacles) {
            if (!_overlap(rect, obstacle, _across(axis))) {
                continue;
            }
            if (by > 0 && obstacle.*axis.Low >= rect.*axis.High) {
                by = std::min(by, obstacle.*axis.Low - rect.*axis.High);
            } else if (by < 0 && obstacle.*axis.High <= rect.*axis.Low) {
                by = std::max(by, obstacle.*axis.High - rect.*axis.Low);
            }
        }
        return by;
    }

    RECT _slide(RECT rect, const LONG by, const std::vector<RECT>& obstacles, const Axis& axis) {
        const LONG travelled = _sweep(rect, by, obstacles, axis);
        rect.*axis.Low += travelled;
        rect.*axis.High += travelled;
        return rect;
    }

    RECT _move(const RECT& start, const POINT delta, const Tiles::Limits& limits, const std::vector<RECT>& obstacles) {
        const auto travel = [&](const Axis& axis) {
            const LONG by = delta.*axis.Delta + _snapShift(start.*axis.Low + delta.*axis.Delta,
                                                            start.*axis.High + delta.*axis.Delta,
                                                            limits.*axis.Guides, limits.Snap);
            return std::max(limits.Bounds.*axis.Low - start.*axis.Low,
                            std::min(by, limits.Bounds.*axis.High - start.*axis.High));
        };
        const LONG dx = travel(Horizontal);
        const LONG dy = travel(Vertical);

        // Both axis orders: sliding along a neighbour works one way round and not the other
        const RECT first = _slide(_slide(start, dx, obstacles, Horizontal), dy, obstacles, Vertical);
        const RECT second = _slide(_slide(start, dy, obstacles, Vertical), dx, obstacles, Horizontal);
        const auto miss = [&](const RECT& rect) {
            return std::abs(rect.left - start.left - dx) + std::abs(rect.top - start.top - dy);
        };
        return miss(first) <= miss(second) ? first : second;
    }

    RECT _resize(const RECT& start, const unsigned grip, const POINT delta, const Tiles::Limits& limits,
                 const std::vector<RECT>& obstacles) {
        RECT rect = start;

        const auto snap = [&](const Axis& axis) {
            if (grip & axis.LowGrip) {
                rect.*axis.Low = _snap(start.*axis.Low + delta.*axis.Delta, limits.*axis.Guides, limits.Snap);
            }
            if (grip & axis.HighGrip) {
                rect.*axis.High = _snap(start.*axis.High + delta.*axis.Delta, limits.*axis.Guides, limits.Snap);
            }
        };

        // Whatever stops an edge wins over the minimum size - a small tile never overlaps
        const auto fit = [&](const Axis& axis) {
            const Axis& across = _across(axis);
            if (grip & axis.LowGrip) {
                LONG low = limits.Bounds.*axis.Low;
                for (const RECT& obstacle : obstacles) {
                    if (_overlap(rect, obstacle, across) && obstacle.*axis.High <= start.*axis.Low) {
                        low = std::max(low, obstacle.*axis.High);
                    }
                }
                rect.*axis.Low = std::max(low, std::min(rect.*axis.Low, rect.*axis.High - limits.MinSize));
            }
            if (grip & axis.HighGrip) {
                LONG high = limits.Bounds.*axis.High;
                for (const RECT& obstacle : obstacles) {
                    if (_overlap(rect, obstacle, across) && obstacle.*axis.Low >= start.*axis.High) {
                        high = std::min(high, obstacle.*axis.Low);
                    }
                }
                rect.*axis.High = std::min(high, std::max(rect.*axis.High, rect.*axis.Low + limits.MinSize));
            }
        };

        snap(Horizontal);
        fit(Horizontal);
        snap(Vertical);
        fit(Vertical);
        // The rows may have grown into an obstacle's, which the first pass did not see
        fit(Horizontal);
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
            if (!_overlap(start, other, Horizontal) || !_overlap(start, other, Vertical)) {
                obstacles.push_back(other);
            }
        }
        return grip == Body ? _move(start, delta, limits, obstacles) : _resize(start, grip, delta, limits, obstacles);
    }
}
