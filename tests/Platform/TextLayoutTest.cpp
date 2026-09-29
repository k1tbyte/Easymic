#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <windows.h>
#include <gdiplus.h>

#include "../Check.hpp"
#include "Platform/Gfx/Gdi.hpp"

namespace {
    using Test::Check;

    void _compare(Gdi::TextLayout& layout, const std::wstring& text, const float size,
                  const wchar_t* family, const int width = 192) {
        const Gdiplus::Font font = Gdi::TextFont(size, family);
        const auto expected = Gdi::MeasureText(text, font);
        const auto actual = layout.Measure(text, size, family);
        Check(actual.Width == expected.Width, "cached width matches fresh measurement");
        Check(std::abs(actual.CenterOffset - expected.CenterOffset) < 0.0001f,
              "cached baseline matches fresh measurement");

        constexpr int Height = 72;
        constexpr int Stride = 192 * 4;
        std::array<uint32_t, 192 * Height> fresh{}, cached{};
        Gdiplus::Bitmap freshImage(192, Height, Stride, PixelFormat32bppARGB, reinterpret_cast<BYTE*>(fresh.data()));
        Gdiplus::Bitmap cachedImage(192, Height, Stride, PixelFormat32bppARGB, reinterpret_cast<BYTE*>(cached.data()));
        {
            Gdiplus::Graphics freshCanvas(&freshImage), cachedCanvas(&cachedImage);
            Gdi::DrawCentred(freshCanvas, text, font,
                            Gdiplus::RectF(0, expected.CenterOffset, static_cast<float>(width), Height));
            layout.Draw(cachedCanvas, width, Height);
            freshCanvas.Flush(Gdiplus::FlushIntentionSync);
            cachedCanvas.Flush(Gdiplus::FlushIntentionSync);
            Check(freshCanvas.GetLastStatus() == Gdiplus::Ok && cachedCanvas.GetLastStatus() == Gdiplus::Ok,
                  "both render paths succeed");
        }
        Check(fresh == cached, "cached rendering is pixel-identical");
    }
}

int main() {
    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token = 0;
    if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) {
        std::puts("FAIL: GDI+ startup");
        return 1;
    }
    {
        Gdi::TextLayout layout;
        _compare(layout, L"EN", 14.72f, L"Segoe UI");
        _compare(layout, L"EN", 14.72f, L"Segoe UI");
        _compare(layout, L"RU", 14.72f, L"Segoe UI");
        _compare(layout, L"Язык изменён", 14.72f, L"Segoe UI");
        _compare(layout, L"Язык изменён", 24.0f, L"Segoe UI");
        _compare(layout, L"Язык изменён", 24.0f, L"Consolas");
        _compare(layout, L"EN ", 24.0f, L"Consolas");
        _compare(layout, L"EN ", 24.0f, L"Consolas", 80);
        layout.Reset();
        layout.Reset();
        _compare(layout, L"EN ", 24.0f, L"Consolas");
        _compare(layout, L"EN", 14.72f, L"Segoe UI");
    }
    Gdiplus::GdiplusShutdown(token);
    std::printf(Test::Failures ? "%d check(s) failed\n" : "all text layout checks passed\n", Test::Failures);
    return Test::Failures ? 1 : 0;
}
