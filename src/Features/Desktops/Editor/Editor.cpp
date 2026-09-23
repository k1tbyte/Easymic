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

    /// The whole editor's state - exactly one editor exists at a time, on the UI thread.
    std::vector<Painter::Tile> _tiles;
    int _dragging = -1;
    unsigned _grip = Tiles::None;
    RECT _startRect{};
    POINT _startMouse{};
    bool _applied = false;
    bool _done = false;
    RECT _donePill{}, _cancelPill{};
    LayeredWindow::Surface _surface;

    POINT _origin() {
        return {GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN)};
    }

    /// Client coordinates, which are the screen's shifted by the virtual screen's corner.
    POINT _toScreen(POINT point) {
        const POINT origin = _origin();
        return {point.x + origin.x, point.y + origin.y};
    }

    RECT _bounds() {
        const POINT origin = _origin();
        return {origin.x, origin.y, origin.x + GetSystemMetrics(SM_CXVIRTUALSCREEN),
                origin.y + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
    }

    Tiles::Limits _limits(const int tile) {
        static std::vector<RECT> obstacles;
        static std::vector<LONG> guidesX, guidesY;
        obstacles.clear();
        guidesX.clear();
        guidesY.clear();

        const RECT bounds = _bounds();
        guidesX = {bounds.left, bounds.right};
        guidesY = {bounds.top, bounds.bottom};
        for (size_t i = 0; i < _tiles.size(); ++i) {
            if (static_cast<int>(i) == tile || _tiles[i].Maximized) {
                continue;
            }
            obstacles.push_back(_tiles[i].Screen);
            guidesX.insert(guidesX.end(), {_tiles[i].Screen.left, _tiles[i].Screen.right});
            guidesY.insert(guidesY.end(), {_tiles[i].Screen.top, _tiles[i].Screen.bottom});
        }
        // Monitor bezels guide too: the taskbar edge is where a maximized window would stop
        EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT rect, LPARAM) -> BOOL {
            guidesX.insert(guidesX.end(), {rect->left, rect->right});
            guidesY.insert(guidesY.end(), {rect->top, rect->bottom});
            MONITORINFO info{.cbSize = sizeof(info)};
            GetMonitorInfoW(monitor, &info);
            guidesY.push_back(info.rcWork.bottom);
            return TRUE;
        }, 0);

        return {obstacles, guidesX, guidesY, bounds, Snap, MinSize};
    }

    void _paint(const HWND hwnd) {
        const RECT bounds = _bounds();
        const POINT at{bounds.left, bounds.top};
        LayeredWindow::Render(hwnd, _surface, bounds.right - bounds.left, bounds.bottom - bounds.top, at,
                              [&](RenderContext& context) {
            // Not fully transparent anywhere: a layered window is hit-tested per pixel, and the
            // dim is what keeps the click off the desktop underneath
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
        // The first tile is drawn last, on top
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
                    RECT moved = Tiles::Drag(_startRect, _grip, delta, _limits(_dragging));
                    if (!EqualRect(&moved, &_tiles[_dragging].Screen)) {
                        _tiles[_dragging].Screen = moved;
                        _paint(hwnd);
                    }
                    return 0;
                }
                unsigned grip = Tiles::None;
                _tileAt(screen, grip);
                const wchar_t* shape = grip == Tiles::None ? IDC_ARROW
                                       : grip == Tiles::Body    ? IDC_SIZEALL
                                       : grip == (Tiles::Left | Tiles::Top) ||
                                         grip == (Tiles::Right | Tiles::Bottom) ? IDC_SIZENWSE
                                       : grip == (Tiles::Right | Tiles::Top) ||
                                         grip == (Tiles::Left | Tiles::Bottom)  ? IDC_SIZENESW
                                       : (grip & (Tiles::Left | Tiles::Right)) ? IDC_SIZEWE
                                                                               : IDC_SIZENS;
                SetCursor(LoadCursorW(nullptr, shape));
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
        RegisterClassW(&windowClass);

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
