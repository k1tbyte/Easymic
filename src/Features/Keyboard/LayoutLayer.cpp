#include "LayoutLayer.hpp"

#include <string>
#include <windows.h>
#include <gdiplus.h>

#include "AppConfig.hpp"
#include "Core/Overlay.hpp"
#include "Gdi.hpp"

namespace {

    /// Quick enough that a switch reads as immediate. Runs only while the pill is on.
    constexpr uint16_t PollMs = 200;

    KeyboardSettings* _settings = nullptr;
    HKL _layout = nullptr;
    std::wstring _text;

    /// @return true when the layout changed since the last look.
    bool _poll() {
        // A layout is per thread, and focus lives on the foreground window's thread. No foreground
        // window - mid-switch - would report this app's own layout, so the last answer stands.
        const HWND foreground = GetForegroundWindow();
        if (!foreground) {
            return false;
        }

        const HKL layout = GetKeyboardLayout(GetWindowThreadProcessId(foreground, nullptr));
        if (layout == _layout) {
            return false;
        }

        _layout = layout;
        wchar_t name[9]{};
        GetLocaleInfoW(MAKELCID(LOWORD(reinterpret_cast<UINT_PTR>(layout)), SORT_DEFAULT),
                       LOCALE_SISO639LANGNAME, name, static_cast<int>(std::size(name)));
        _text = CharUpperW(name);
        return true;
    }

    OverlaySlot _measure(const OverlayCell& cell) {
        if (!_settings->ShowLayout) {
            return {};
        }

        _poll();
        return {.Width = Gdi::MeasureText(_text, Gdi::TextFont(cell.FontSize)) + cell.Padding * 2};
    }

    void _render(const OverlayCell& cell, Gdiplus::Graphics& canvas, const int width) {
        Gdi::DrawCentred(canvas, _text, Gdi::TextFont(cell.FontSize),
                         Gdiplus::RectF(0, 0, static_cast<Gdiplus::REAL>(width),
                                        static_cast<Gdiplus::REAL>(cell.Height)));
    }

    void _tick() {
        if (_poll()) {
            Overlay::Changed();
        }
    }

} // anonymous namespace

namespace LayoutLayer {

    void Register(KeyboardSettings& settings) {
        _settings = &settings;
        Overlay::Add({.Id = "kbd.layout",
                      .Measure = &_measure,
                      .Render = &_render,
                      .TickMs = PollMs,
                      .Tick = &_tick});
    }
}
