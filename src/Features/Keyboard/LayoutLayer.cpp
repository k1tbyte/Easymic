#include "LayoutLayer.hpp"

#include <string>
#include <gdiplus.h>

#include "AppConfig.hpp"
#include "Core/Dispatcher.hpp"
#include "Core/Lifecycle.hpp"
#include "Core/Overlay.hpp"
#include "Gdi.hpp"
#include "InputLanguage.hpp"
#include "Platform/Str.hpp"

namespace {

    KeyboardSettings* _settings = nullptr;
    HKL _layout = nullptr;
    std::wstring _text;
    float _centerOffset = 0.0f;
    bool _watching = false;
    HWINEVENTHOOK _hooks[2]{};

    bool _show(const HKL layout) {
        if (!layout || layout == _layout) {
            return false;
        }

        _layout = layout;
        _text = Str::Utf8ToWide(InputLanguage::Iso639(layout));
        CharUpperBuffW(_text.data(), static_cast<DWORD>(_text.size()));
        return true;
    }

    /// Focus may sit in another thread or process (an embedded browser), and that thread is the one a switch changes.
    /// No foreground window (mid-switch) would report this app's own layout: the last answer stands.
    bool _read() {
        const HWND target = InputLanguage::FocusedWindow();
        return target && _show(InputLanguage::LayoutOf(target));
    }

    /// No cross-process notice of a layout change exists: a switching thread creates a new IME window once the switch
    /// is in (the destroy comes too early).
    void CALLBACK _onEvent(HWINEVENTHOOK, DWORD, HWND, const LONG object, LONG, DWORD, DWORD) {
        if (object == OBJID_WINDOW && _read()) {
            Overlay::Changed();
        }
    }

    void _watch(const bool on) {
        if (on == _watching) {
            return;
        }

        _watching = on;
        constexpr DWORD Events[] = {EVENT_SYSTEM_FOREGROUND, EVENT_OBJECT_CREATE};
        for (size_t i = 0; i < std::size(Events); i++) {
            if (on) {
                _hooks[i] = SetWinEventHook(Events[i], Events[i], nullptr, &_onEvent, 0, 0,
                                            WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
            } else if (_hooks[i]) {
                UnhookWinEvent(_hooks[i]);
                _hooks[i] = nullptr;
            }
        }
    }

    OverlaySlot _measure(const OverlayCell& cell) {
        if (!_settings->ShowLayout) {
            return {};
        }

        _read();
        const auto metrics = Gdi::MeasureText(_text, Gdi::TextFont(cell.FontSize, cell.FontFamily));
        _centerOffset = metrics.CenterOffset;
        return {.Width = metrics.Width + cell.Padding * 2};
    }

    void _render(const OverlayCell& cell, Gdiplus::Graphics& canvas, const int width) {
        Gdi::DrawCentred(canvas, _text, Gdi::TextFont(cell.FontSize, cell.FontFamily),
                         Gdiplus::RectF(0, _centerOffset, static_cast<Gdiplus::REAL>(width),
                                        static_cast<Gdiplus::REAL>(cell.Height)));
    }

} // anonymous namespace

namespace LayoutLayer {

    void Register(KeyboardSettings& settings) {
        _settings = &settings;
        Lifecycle::Restore += [] { _watch(_settings->ShowLayout); };
        Overlay::Add({.Id = "kbd.layout", .Measure = &_measure, .Render = &_render});
    }

    void Requested(const HKL layout) {
        Dispatcher::ToUi([layout] {
            if (_settings->ShowLayout && _show(layout)) {
                Overlay::Changed();
            }
        });
    }
}
