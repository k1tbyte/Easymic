#pragma once

#include <functional>
#include <windows.h>
#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")

/// The canvas a render pass draws on, and the area it owns.
struct RenderContext {
    Gdiplus::Graphics* graphics = nullptr;
    int width = 0;
    int height = 0;
    /// Of the whole window, for the callback to lower.
    BYTE alpha = 255;
};

namespace LayeredWindow {

    using RenderCallback = std::function<void(RenderContext&)>;

    /**
     * @brief Back buffer for a layered window.
     *
     * Built once per size and kept across paints. Every layered window owns its own: a second
     * window's size would churn a shared one on every repaint.
     */
    class Surface {
        HDC _dc = nullptr;
        HBITMAP _bitmap = nullptr;
        HGDIOBJ _previous = nullptr;
        int _width = 0;
        int _height = 0;

    public:
        HDC Get(HDC screenDC, const int width, const int height) {
            if (_dc && _width == width && _height == height) {
                return _dc;
            }

            Release();

            BITMAPINFO info = {};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = width;
            info.bmiHeader.biHeight = -height; // Top-down
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;

            void* bits = nullptr;
            _bitmap = CreateDIBSection(screenDC, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (!_bitmap) {
                return nullptr;
            }

            _dc = CreateCompatibleDC(screenDC);
            if (!_dc) {
                DeleteObject(_bitmap);
                _bitmap = nullptr;
                return nullptr;
            }

            _previous = SelectObject(_dc, _bitmap);
            _width = width;
            _height = height;
            return _dc;
        }

        void Release() {
            if (_dc) {
                SelectObject(_dc, _previous);
                DeleteDC(_dc);
                _dc = nullptr;
            }
            if (_bitmap) {
                DeleteObject(_bitmap);
                _bitmap = nullptr;
            }
            _width = _height = 0;
        }

        ~Surface() { Release(); }
    };

    /**
     * @brief Draws through the callback and pushes the result to the layered window.
     */
    inline void Render(HWND hwnd, Surface& surface, int width, int height, const POINT& windowPos,
                       const RenderCallback& renderFunc) {
        if (!hwnd || !renderFunc) {
            return;
        }

        HDC screenDC = GetDC(hwnd);
        if (!screenDC) {
            return;
        }

        HDC memoryDC = surface.Get(screenDC, width, height);
        if (!memoryDC) {
            ReleaseDC(hwnd, screenDC);
            return;
        }

        Gdiplus::Graphics graphics(memoryDC);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        graphics.Clear(Gdiplus::Color(0, 0, 0, 0));

        RenderContext ctx{&graphics, width, height};
        renderFunc(ctx);

        POINT source = {0, 0};
        POINT destination = windowPos;
        SIZE size = {width, height};
        BLENDFUNCTION blend = {AC_SRC_OVER, 0, ctx.alpha, AC_SRC_ALPHA};

        UpdateLayeredWindow(hwnd, screenDC, &destination, &size, memoryDC, &source, 0, &blend, ULW_ALPHA);

        ReleaseDC(hwnd, screenDC);
    }
}

