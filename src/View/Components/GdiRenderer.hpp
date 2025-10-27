#ifndef EASYMIC_GDIRENDERER_HPP
#define EASYMIC_GDIRENDERER_HPP

#include <windows.h>
#include <gdiplus.h>
#include <memory>

#pragma comment(lib, "gdiplus.lib")

/**
 * @brief RAII-обертка для GDI+ инициализации
 */
class GdiPlusContext {
public:
    GdiPlusContext() {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&token_, &input, nullptr);
    }

    ~GdiPlusContext() {
        Gdiplus::GdiplusShutdown(token_);
    }

    GdiPlusContext(const GdiPlusContext&) = delete;
    GdiPlusContext& operator=(const GdiPlusContext&) = delete;

private:
    ULONG_PTR token_ = 0;
};

/**
 * @brief Помощник для рендеринга с GDI+
 * Инкапсулирует сложную логику работы с layered windows
 */
class LayeredWindowRenderer {
public:
    struct RenderContext {
        Gdiplus::Graphics* graphics = nullptr;
        int width = 0;
        int height = 0;
    };

    using RenderCallback = std::function<void(RenderContext&)>;

    /**
     * @brief Рендеринг в layered окно
     * @param hwnd Handle окна
     * @param width Ширина
     * @param height Высота
     * @param renderFunc Callback для рисования
     */
    static void Render(HWND hwnd, int width, int height, const RenderCallback& renderFunc) {
        if (!hwnd || !renderFunc) {
            return;
        }

        HDC hdcScreen = GetDC(nullptr);
        if (!hdcScreen) {
            return;
        }

        HDC hdcMem = CreateCompatibleDC(hdcScreen);
        if (!hdcMem) {
            ReleaseDC(nullptr, hdcScreen);
            return;
        }

        // Создание 32-bit bitmap с alpha каналом
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = width;
        bmi.bmiHeader.biHeight = -height; // Top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* pvBits = nullptr;
        HBITMAP hBitmap = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pvBits, nullptr, 0);
        if (!hBitmap) {
            DeleteDC(hdcMem);
            ReleaseDC(nullptr, hdcScreen);
            return;
        }

        HBITMAP hOldBitmap = static_cast<HBITMAP>(SelectObject(hdcMem, hBitmap));

        // Настройка GDI+ Graphics
        Gdiplus::Graphics graphics(hdcMem);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        graphics.Clear(Gdiplus::Color(0, 0, 0, 0));

        // Вызов callback для рисования
        RenderContext ctx{&graphics, width, height};
        renderFunc(ctx);

        // Обновление layered window
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

    /**
     * @brief Создание rounded rectangle path
     */
    static void CreateRoundedRectPath(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rect, float radius) {
        float diameter = radius * 2.0f;

        // Top-left
        path.AddArc(rect.X, rect.Y, diameter, diameter, 180, 90);
        // Top-right
        path.AddArc(rect.X + rect.Width - diameter, rect.Y, diameter, diameter, 270, 90);
        // Bottom-right
        path.AddArc(rect.X + rect.Width - diameter, rect.Y + rect.Height - diameter, diameter, diameter, 0, 90);
        // Bottom-left
        path.AddArc(rect.X, rect.Y + rect.Height - diameter, diameter, diameter, 90, 90);

        path.CloseFigure();
    }
};

#endif //EASYMIC_GDIRENDERER_HPP

