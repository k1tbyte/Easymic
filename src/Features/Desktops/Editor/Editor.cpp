#include "Editor.hpp"

#include "AppConfig.hpp"
#include "../Painter.hpp"
#include "Gdi.hpp"
#include "LayeredWindow.hpp"
#include "Tiles.hpp"

#include <algorithm>
#include <optional>
#include <windowsx.h>

namespace {

    constexpr wchar_t EditorClass[] = L"EasyLauncher.DesktopEditor";
    constexpr int GripBorder = 8;
    constexpr int Snap = 12;
    constexpr int MinSize = 120;
    constexpr int PillWidth = 120;
    constexpr int PillHeight = 36;
    constexpr int PillGap = 16;
    constexpr int PillBottom = 40;

    /// Nothing a drag reads changes while it runs, so it is measured once at the press.
    struct Constraints {
        std::vector<RECT> Obstacles;
        std::vector<LONG> GuidesX, GuidesY;
        RECT Bounds{};

        Tiles::Limits View() const {
            return {Obstacles, GuidesX, GuidesY, Bounds, Snap, MinSize};
        }
    };

    // One editor at a time, on the UI thread
    std::vector<Painter::Tile> _tiles;
    int _dragging = -1;
    unsigned _grip = Tiles::None;
    RECT _startRect{};
    POINT _startMouse{};
    Constraints _constraints;
    bool _applied = false;
    bool _done = false;
    RECT _donePill{}, _cancelPill{};
    LayeredWindow::Surface _surface;

    POINT _origin() {
        return {GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN)};
    }

    POINT _toScreen(POINT point) {
        const POINT origin = _origin();
        return {point.x + origin.x, point.y + origin.y};
    }

    RECT _bounds() {
        const POINT origin = _origin();
        return {origin.x, origin.y, origin.x + GetSystemMetrics(SM_CXVIRTUALSCREEN),
                origin.y + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
    }

    Constraints _constraintsFor(const int dragged) {
        Constraints constraints{.Bounds = _bounds()};
        constraints.GuidesX = {constraints.Bounds.left, constraints.Bounds.right};
        constraints.GuidesY = {constraints.Bounds.top, constraints.Bounds.bottom};
        for (size_t i = 0; i < _tiles.size(); ++i) {
            if (static_cast<int>(i) == dragged || _tiles[i].Maximized) {
                continue;
            }
            const RECT& screen = _tiles[i].Screen;
            constraints.Obstacles.push_back(screen);
            constraints.GuidesX.insert(constraints.GuidesX.end(), {screen.left, screen.right});
            constraints.GuidesY.insert(constraints.GuidesY.end(), {screen.top, screen.bottom});
        }
        // Monitor edges guide too, and the taskbar edge is where a maximized window would stop
        EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT rect, LPARAM param) -> BOOL {
            auto& monitors = *reinterpret_cast<Constraints*>(param);
            monitors.GuidesX.insert(monitors.GuidesX.end(), {rect->left, rect->right});
            monitors.GuidesY.insert(monitors.GuidesY.end(), {rect->top, rect->bottom});
            MONITORINFO info{.cbSize = sizeof(info)};
            GetMonitorInfoW(monitor, &info);
            monitors.GuidesY.push_back(info.rcWork.bottom);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&constraints));

        return constraints;
    }

    void _paint(const HWND hwnd) {
        const RECT bounds = _bounds();
        const POINT at{bounds.left, bounds.top};
        LayeredWindow::Render(hwnd, _surface, bounds.right - bounds.left, bounds.bottom - bounds.top, at,
                              [&](RenderContext& context) {
            // A layered window is hit-tested per pixel: the dim keeps the click off the desktop underneath
            context.graphics->Clear(Gdiplus::Color(130, 12, 12, 14));

            Painter::Tiles(*context.graphics, _tiles, static_cast<float>(-bounds.left),
                           static_cast<float>(-bounds.top), 1.f, 13.f);

            if (_dragging < 0) {
                const Gdiplus::Font font = Gdi::TextFont(15.f);
                for (const RECT* pill : {&_donePill, &_cancelPill}) {
                    Gdiplus::RectF atPill(static_cast<Gdiplus::REAL>(pill->left - bounds.left),
                                          static_cast<Gdiplus::REAL>(pill->top - bounds.top),
                                          static_cast<Gdiplus::REAL>(pill->right - pill->left),
                                          static_cast<Gdiplus::REAL>(pill->bottom - pill->top));
                    Gdiplus::GraphicsPath path;
                    Gdi::RoundedRect(path, atPill, PillHeight / 2.f);
                    const Gdiplus::SolidBrush fill(Gdiplus::Color(220, 45, 45, 50));
                    context.graphics->FillPath(&fill, &path);
                    Gdi::DrawCentred(*context.graphics, pill == &_donePill ? L"Done" : L"Cancel",
                                     font, atPill);
                }
            }
        });
    }

    int _tileAt(const POINT screen, unsigned& grip) {
        // The first tile is drawn on top, so it wins the hit
        for (size_t i = 0; i < _tiles.size(); ++i) {
            if (_tiles[i].Maximized) {
                continue;
            }
            if (const unsigned hit = Tiles::HitTest(_tiles[i].Screen, screen, GripBorder)) {
                grip = hit;
                return static_cast<int>(i);
            }
        }
        grip = Tiles::None;
        return -1;
    }

    const wchar_t* _cursorFor(const unsigned grip) {
        switch (grip) {
            case Tiles::None: return IDC_ARROW;
            case Tiles::Body: return IDC_SIZEALL;
            case Tiles::Left | Tiles::Top:
            case Tiles::Right | Tiles::Bottom: return IDC_SIZENWSE;
            case Tiles::Right | Tiles::Top:
            case Tiles::Left | Tiles::Bottom: return IDC_SIZENESW;
            default: return grip & (Tiles::Left | Tiles::Right) ? IDC_SIZEWE : IDC_SIZENS;
        }
    }

    void _end(const bool apply) {
        _applied = apply;
        _done = true;
    }

    LRESULT CALLBACK _proc(const HWND hwnd, const UINT message, const WPARAM wParam, const LPARAM lParam) {
        switch (message) {
            case WM_PAINT:
                _paint(hwnd);
                return 0;

            case WM_LBUTTONDOWN: {
                const POINT screen = _toScreen({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
                if (PtInRect(&_donePill, screen)) {
                    _end(true);
                    return 0;
                }
                if (PtInRect(&_cancelPill, screen)) {
                    _end(false);
                    return 0;
                }
                _dragging = _tileAt(screen, _grip);
                if (_dragging >= 0) {
                    _constraints = _constraintsFor(_dragging);
                    _startRect = _tiles[_dragging].Screen;
                    _startMouse = screen;
                    _tiles[_dragging].Active = true;
                    SetCapture(hwnd);
                    _paint(hwnd);
                }
                return 0;
            }

            case WM_MOUSEMOVE: {
                const POINT screen = _toScreen({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
                if (_dragging >= 0) {
                    const POINT delta{screen.x - _startMouse.x, screen.y - _startMouse.y};
                    const RECT moved = Tiles::Drag(_startRect, _grip, delta, _constraints.View());
                    if (!EqualRect(&moved, &_tiles[_dragging].Screen)) {
                        _tiles[_dragging].Screen = moved;
                        _paint(hwnd);
                    }
                    return 0;
                }
                unsigned grip = Tiles::None;
                _tileAt(screen, grip);
                SetCursor(LoadCursorW(nullptr, _cursorFor(grip)));
                return 0;
            }

            case WM_LBUTTONUP:
                if (_dragging >= 0) {
                    _tiles[_dragging].Active = false;
                    _dragging = -1;
                    ReleaseCapture();
                    _paint(hwnd);
                }
                return 0;

            case WM_KEYDOWN:
                if (wParam == VK_RETURN) {
                    _end(true);
                } else if (wParam == VK_ESCAPE) {
                    _end(false);
                }
                return 0;

            // Alt+F4: the loop in Run owns the window, DefWindowProc must not destroy it
            case WM_CLOSE:
                _end(false);
                return 0;

            default:
                break;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

} // anonymous namespace

namespace Editor {

    void Run(DesktopPreset& preset) {
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        const WNDCLASSW windowClass{.lpfnWndProc = _proc,
                                    .hInstance = instance,
                                    .hCursor = LoadCursorW(nullptr, IDC_ARROW),
                                    .lpszClassName = EditorClass};
        static const bool registered = RegisterClassW(&windowClass) != 0;
        if (!registered) {
            return;
        }

        const RECT bounds = _bounds();
        const int width = bounds.right - bounds.left;
        const int height = bounds.bottom - bounds.top;
        _donePill = {bounds.left + width / 2 - PillWidth - PillGap / 2, bounds.bottom - PillBottom - PillHeight,
                     bounds.left + width / 2 - PillGap / 2, bounds.bottom - PillBottom};
        _cancelPill = {bounds.left + width / 2 + PillGap / 2, _donePill.top,
                       bounds.left + width / 2 + PillWidth + PillGap / 2, _donePill.bottom};

        _tiles = Painter::FromRules(preset.Windows);
        _dragging = -1;
        _applied = false;
        _done = false;

        const HWND hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                                          EditorClass, nullptr, WS_POPUP,
                                          bounds.left, bounds.top, width, height,
                                          nullptr, nullptr, instance, nullptr);
        if (!hwnd) {
            return;
        }

        // One paint before the window shows, or the first frame is the untouched desktop
        _paint(hwnd);
        ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);

        MSG message;
        while (!_done && GetMessageW(&message, nullptr, 0, 0)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        DestroyWindow(hwnd);
        _surface.Release();

        if (!_applied) {
            return;
        }
        for (const Painter::Tile& tile : _tiles) {
            if (tile.Maximized) {
                continue;
            }
            WindowRule& rule = preset.Windows[tile.Rule];
            rule.X = tile.Screen.left;
            rule.Y = tile.Screen.top;
            rule.Width = tile.Screen.right - tile.Screen.left;
            rule.Height = tile.Screen.bottom - tile.Screen.top;
        }
    }
}
