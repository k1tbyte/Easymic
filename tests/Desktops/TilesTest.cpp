#include <cstdio>
#include <vector>

#include "../Check.hpp"
#include "Features/Desktops/Editor/Tiles.hpp"

namespace {

    using Test::Check;
    using namespace Tiles;

    const LONG Guides[] = {0, 1920};
    const LONG GuidesWithMid[] = {0, 500, 1920};
    const LONG RowGuides[] = {0, 1080};
    const RECT Bounds{0, 0, 1920, 1080};

    bool _same(const RECT& a, const RECT& b) {
        return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
    }

    RECT _drag(const RECT& start, const unsigned grip, const POINT delta, const std::vector<RECT>& obstacles = {},
               const std::span<const LONG> guidesX = Guides) {
        return Drag(start, grip, delta, Limits{obstacles, guidesX, RowGuides, Bounds, 12, 120});
    }

    void _hitTest() {
        const RECT tile{100, 100, 400, 300};
        Check(HitTest(tile, {50, 50}, 8) == None, "outside");
        Check(HitTest(tile, {400, 200}, 8) == None, "right edge is outside");
        Check(HitTest(tile, {100, 100}, 8) == (Left | Top), "top left corner");
        Check(HitTest(tile, {399, 299}, 8) == (Right | Bottom), "bottom right corner");
        Check(HitTest(tile, {250, 102}, 8) == Top, "top edge");
        Check(HitTest(tile, {250, 299}, 8) == Bottom, "bottom edge");
        Check(HitTest(tile, {102, 200}, 8) == Left, "left edge");
        Check(HitTest(tile, {399, 200}, 8) == Right, "right edge");
        Check(HitTest(tile, {250, 200}, 8) == Body, "inside");
        Check(HitTest(tile, {108, 200}, 8) == Body, "the border is exclusive");
    }

    void _move() {
        const RECT start{100, 100, 400, 300};
        Check(_same(_drag(start, Body, {50, 30}), {150, 130, 450, 330}), "free move");
        Check(_same(_drag(start, Body, {97, 0}, {}, GuidesWithMid), {200, 100, 500, 300}), "edge snaps to a guide");
        Check(_same(_drag(start, Body, {5000, 0}), {1620, 100, 1920, 300}), "clamped to the bounds");
        Check(_same(_drag(start, Body, {300, 0}, {{500, 100, 700, 300}}), {200, 100, 500, 300}), "stops at an obstacle");
        Check(_same(_drag(start, Body, {400, 0}, {{300, 200, 600, 400}}), {500, 100, 800, 300}),
              "an obstacle already overlapping the start is no obstacle");
        Check(_same(_drag(start, Body, {100, 100}, {{400, 150, 600, 250}}), {100, 200, 400, 400}),
              "slides along a neighbour");
    }

    void _resize() {
        const RECT start{100, 100, 400, 300};
        Check(_same(_drag(start, Right, {500, 0}, {{700, 100, 900, 300}}), {100, 100, 700, 300}), "right edge stops at an obstacle");
        Check(_same(_drag(start, Right, {-290, 0}), {100, 100, 220, 300}), "minimum size");
        Check(_same(_drag({500, 100, 800, 300}, Left, {-600, 0}), {0, 100, 800, 300}), "left edge stops at the bounds");
        Check(_same(_drag(start, Bottom, {0, 775}), {100, 100, 400, 1080}), "bottom edge snaps to the guide");
        Check(_same(_drag(start, Left | Top, {-1000, -1000}), {0, 0, 400, 300}), "corner");
        Check(_same(_drag({100, 300, 400, 500}, Top, {0, -250}, {{100, 100, 400, 250}}), {100, 250, 400, 500}),
              "top edge stops at an obstacle above");
        Check(_same(_drag({100, 100, 300, 200}, Right | Bottom, {300, 100}, {{350, 250, 600, 400}}), {100, 100, 600, 250}),
              "corner stops at the obstacle below, not the one aside");
    }
}

int main() {
    _hitTest();
    _move();
    _resize();

    std::printf(Test::Failures ? "%d check(s) failed\n" : "all tile checks passed\n", Test::Failures);
    return Test::Failures ? 1 : 0;
}
