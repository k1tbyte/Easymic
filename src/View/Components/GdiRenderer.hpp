#ifndef EASYMIC_GDIRENDERER_HPP
#define EASYMIC_GDIRENDERER_HPP

#include <windows.h>
#include <gdiplus.h>
#include <memory>

#pragma comment(lib, "gdiplus.lib")

struct RenderContext {
    Gdiplus::Graphics* graphics = nullptr;
    int width = 0;
    int height = 0;
};




namespace GDIRenderer {

    using RenderCallback = std::function<void(RenderContext&)>;

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
