#pragma once

#include <array>
#include <string>
#include <utility>
#include <windows.h>

#include "AppConfig.hpp"
#include "Core/Overlay.hpp"
#include "OverlaySlots.hpp"
#include "OverlayWindow.hpp"
#include "TextLayer.hpp"
#include "Str.hpp"
#include "System/UACService.hpp"

/// Where the overlay sits, how wide it is and whether it is on screen. What is drawn in it is the layers' business.
class OverlaySurface {
    static constexpr UINT_PTR ID_TEXT_TIMER = WM_USER + 101;
    static constexpr UINT_PTR ID_LAYER_TIMER = WM_USER + 200;
    static constexpr int TextDurationMs = 2000;

    OverlayWindow& _window;
    OverlaySettings& _cfg;
    std::string _fontSource;
    std::wstring _fontFamily = L"Segoe UI";

    OverlaySlots _slots;
    /// Where the user left the overlay; widening it for a notification must not move this.
    POINT _anchor{};
    POINT _previewOrigin{};
    bool _previewPlaced = false;
    bool _preview = false;
    std::array<bool, Overlay::MaxLayers> _ticking{};

    /// True when the family changed. Runs only once something is on screen, so an idle app never touches GDI+ fonts.
    bool _syncFont() {
        if (_fontSource == _cfg.FontFamily) {
            return false;
        }
        _fontSource = _cfg.FontFamily;
        std::wstring family = Str::Utf8ToWide(_fontSource);
        if (family.empty() || Gdiplus::FontFamily(family.c_str()).GetLastStatus() != Gdiplus::Ok) {
            family = L"Segoe UI";
        }
        if (family == _fontFamily) {
            return false;
        }
        _fontFamily = std::move(family);
        return true;
    }

    void _measure() {
        _slots = OverlaySlots::Measure(_cfg.Size, _preview, _fontFamily.c_str());
        if (_slots.TotalWidth && _syncFont()) {
            _slots = OverlaySlots::Measure(_cfg.Size, _preview, _fontFamily.c_str());
        }
    }

    /// A pill that grew must not shove the first one sideways: the strip grows right, or left at the work area's edge.
    LONG _originX() const {
        const int overhang = _slots.TotalWidth - _slots.Height;
        if (overhang <= 0) {
            return _anchor.x;
        }

        MONITORINFO info{sizeof(MONITORINFO)};
        const HMONITOR monitor = MonitorFromPoint(_anchor, MONITOR_DEFAULTTONEAREST);
        const bool spillsOver = monitor && GetMonitorInfoW(monitor, &info)
                                && _anchor.x + _slots.TotalWidth > info.rcWork.right;

        return spillsOver ? _anchor.x - overhang : _anchor.x;
    }

    void _capturePreviewPosition() {
        _window.SyncBounds();
        _anchor.x += _window.Pos.x - _previewOrigin.x;
        _anchor.y += _window.Pos.y - _previewOrigin.y;
        _previewOrigin = _window.Pos;
    }

    void _syncTimers() {
        for (size_t i = 0; i < _slots.Count; i++) {
            const OverlayLayer& layer = Overlay::Layers[i];
            const bool wanted = layer.TickMs && layer.Tick && _slots.Items[i].Slot.WantsTick;

            if (wanted == _ticking[i]) {
                continue;
            }

            wanted ? SetTimer(_window.Frame(), ID_LAYER_TIMER + i, layer.TickMs, nullptr)
                   : KillTimer(_window.Frame(), ID_LAYER_TIMER + i);
            _ticking[i] = wanted;
        }
    }

    void _killTimers() {
        for (size_t i = 0; i < _ticking.size(); i++) {
            if (_ticking[i]) {
                KillTimer(_window.Frame(), ID_LAYER_TIMER + i);
                _ticking[i] = false;
            }
        }

        _killText();
    }

    void _killText() {
        if (TextLayer::Text.empty()) {
            return;
        }

        KillTimer(_window.Frame(), ID_TEXT_TIMER);
        TextLayer::Text.clear();
    }

public:
    OverlaySurface(OverlayWindow& window, OverlaySettings& config) : _window(window), _cfg(config) {
        const int size = OverlaySlots::PillHeight(config.Size);
        _window.Pos = {config.PosX, config.PosY};
        _window.Size = {size, size};

        Overlay::Add({.Id = "overlay.text",
                      .Order = Overlay::Last,
                      .Measure = &TextLayer::Measure,
                      .Render = &TextLayer::Render});
    }

    void Relayout() {
        // While nothing has widened the strip the window is the anchor: dragging is stored that way.
        // Reading it back under a pill would walk it left notification by notification (work-area clamp).
        if (_preview && _previewPlaced) {
            _capturePreviewPosition();
        } else if (_slots.TotalWidth <= _slots.Height) {
            _anchor = _window.Pos;
        }

        _measure();
        _syncTimers();

        if (!_slots.TotalWidth) {
            // Parked on the anchor first: Place writes the work-area clamp into Pos, and a hidden
            // window would keep it and be read back as the anchor.
            _window.Pos = _anchor;
            _window.Hide();
            return;
        }

        _window.Pos = {_originX(), _anchor.y};
        _window.Size = {_slots.TotalWidth, _slots.Height};
        _window.Show();
        _window.Place(HWND_TOPMOST);
        if (_preview) {
            _previewOrigin = _window.Pos;
            _previewPlaced = true;
        }
        _window.Invalidate();
    }

    void Render(RenderContext& context) const {
        context.alpha = static_cast<BYTE>(std::min<int>(_cfg.Opacity, 100) * 255 / 100);
        _slots.Render(*context.graphics);
    }

    void OnTimer(const UINT_PTR timerId) {
        if (timerId == ID_TEXT_TIMER) {
            _killText();
            Relayout();
            return;
        }

        const size_t index = timerId - ID_LAYER_TIMER;
        if (index < _slots.Count) {
            Overlay::Layers[index].Tick();
        }
    }

    void ShowText(std::wstring text) {
        TextLayer::Text = std::move(text);
        // Same id: a second action while the first is up restarts the countdown
        SetTimer(_window.Frame(), ID_TEXT_TIMER, TextDurationMs, nullptr);
        Relayout();
    }

    void Suspend() {
        _killTimers();
        _preview = true;
        _previewPlaced = false;

        if (_window.OnShadow()) {
            _window.LeaveShadow();
        }

        Relayout();
    }

    void CommitPosition() {
        if (_previewPlaced) {
            _capturePreviewPosition();
        }
        _cfg.PosX = _anchor.x;
        _cfg.PosY = _anchor.y;
    }

    void Restore() {
        _killText();
        _preview = false;
        _previewPlaced = false;
        _anchor = {_cfg.PosX, _cfg.PosY};
        _window.Pos = _anchor;

        if (_cfg.OnTopExclusive && UAC::IsElevated() && !_window.OnShadow()) {
            _window.Hide();
            _window.EnterShadow();
            _window.Place(HWND_TOPMOST);
        }

        _window.SetDisplayAffinity(_cfg.ExcludeFromCapture ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE);
        Relayout();
    }
};
