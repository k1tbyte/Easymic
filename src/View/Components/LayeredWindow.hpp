//
// Created by kitbyte on 01.11.2025.
//

#ifndef EASYMIC_LAYEREDWINDOW_H
#define EASYMIC_LAYEREDWINDOW_H

#include <windows.h>
#include <gdiplus.h>
#include <memory>
#include "GdiRenderer.hpp"

#pragma comment(lib, "gdiplus.lib")

/**
 * @brief Rendering to layered window
 * @param hwnd Window handle
 * @param width Width
 * @param height Height
 * @param renderFunc Drawing callback
 */

namespace GDIRenderer {
    static void RenderLayeredWindow(HWND hwnd, int width, int height, const RenderCallback &renderFunc) {
        if (!hwnd || !renderFunc) {
            return;
        }

        HDC hdcScreen = GetDC(hwnd);
        if (!hdcScreen) {
            return;
        }

        HDC hdcMem = CreateCompatibleDC(hdcScreen);
        if (!hdcMem) {
            ReleaseDC(nullptr, hdcScreen);
            return;
        }

        // Create 32-bit bitmap with alpha channel
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = width;
        bmi.bmiHeader.biHeight = -height; // Top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void *pvBits = nullptr;
        HBITMAP hBitmap = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pvBits, nullptr, 0);
        if (!hBitmap) {
            DeleteDC(hdcMem);
            ReleaseDC(nullptr, hdcScreen);
            return;
        }

        HBITMAP hOldBitmap = static_cast<HBITMAP>(SelectObject(hdcMem, hBitmap));

        // Configure GDI+ Graphics
        Gdiplus::Graphics graphics(hdcMem);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        graphics.Clear(Gdiplus::Color(0, 0, 0, 0));

        // Invoke callback to draw
        RenderContext ctx{&graphics, width, height};
        renderFunc(ctx);

        // Update layered window
        RECT rcWnd;
        GetWindowRect(hwnd, &rcWnd);

        POINT ptSrc = {0, 0};
        POINT ptDst = {rcWnd.left, rcWnd.top};
        SIZE sizeWnd = {width, height};
        BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};

        UpdateLayeredWindow(hwnd, hdcScreen, &ptDst, &sizeWnd, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);

        // Cleanup
        SelectObject(hdcMem, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(hdcMem);
        ReleaseDC(nullptr, hdcScreen);
    }
}

#endif //EASYMIC_LAYEREDWINDOW_H
