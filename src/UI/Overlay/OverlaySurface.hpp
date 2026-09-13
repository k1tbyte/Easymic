#pragma once

#include <array>
#include <string>
#include <utility>
#include <windows.h>

#include "AppConfig.hpp"
#include "Core/Overlay.hpp"
#include "MainWindow.hpp"
#include "OverlaySlots.hpp"
#include "TextLayer.hpp"
#include "UACService.hpp"
#include "UIAccess/UIAccessManager.hpp"

/**
 * @brief Where the overlay sits, how wide it is, and whether it is on screen at all.
 *
 * Owns nothing that is drawn in it - that is the layer registry's job. It drives the frame's
 * window rather than one of its own: whoever owns the message loop is who the worker gets back to
 * the UI thread through, and there is no second answer to that.
 */
class OverlaySurface {
    static constexpr UINT_PTR ID_TEXT_TIMER = WM_USER + 101;
    /// One timer per layer that asks for one, offset by the layer's index - two layers can poll
    /// at different rates without agreeing on a common interval.
    static constexpr UINT_PTR ID_LAYER_TIMER = WM_USER + 200;
    static constexpr int TextDurationMs = 2000;
    /// Kept as it was: the shadow window is looked up by name from another process, and renaming
    /// it would orphan the one a running instance already made.
    static constexpr const char* ShadowWindowKey = "EasyLauncherIndicator";

    MainWindow* _view;
    OverlaySettings& _cfg;

    OverlaySlots _slots;
    /// Where the user left the overlay. Widening it for a notification must not move this.
    POINT _anchor{};
    /// The settings window is open, so every layer shows a state rather than nothing - the user
    /// is dragging the overlay and has to see what they are dragging.
    bool _preview = false;
    std::array<bool, Overlay::MaxLayers> _ticking{};

    /// A pill that grew must not shove the first one sideways, so the strip grows to the right -
    /// or to the left instead, when the right edge of the work area is in the way.
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

    void _applyDisplayAffinity() const {
        const auto affinity = _cfg.ExcludeFromCapture ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE;
        DWORD existingAffinity = 0;
        GetWindowDisplayAffinity(_view->GetEffectiveHandle(), &existingAffinity);

        if (existingAffinity == affinity) {
            return;
        }

        _view->IsOvershadowed()
            ? UIAccessManager::InjectDisplayAffinity(_view->GetEffectiveHandle(), affinity)
            : SetWindowDisplayAffinity(_view->GetHandle(), affinity);
    }

    /// A layer is polled while it has something on screen, or while it asked to be polled
    /// anyway - which is how it watches for its own cue with no window to host a timer.
    void _syncTimers() {
        for (size_t i = 0; i < _slots.Count; i++) {
            const OverlayLayer& layer = Overlay::Layers[i];
            const OverlaySlot& slot = _slots.Items[i].Slot;
            const bool wanted = layer.TickMs && layer.Tick && (slot.Width > 0 || slot.WantsTick);

            if (wanted == _ticking[i]) {
                continue;
            }

            wanted ? SetTimer(_view->GetHandle(), ID_LAYER_TIMER + i, layer.TickMs, nullptr)
                   : KillTimer(_view->GetHandle(), ID_LAYER_TIMER + i);
            _ticking[i] = wanted;
        }
    }

    void _killTimers() {
        for (size_t i = 0; i < _ticking.size(); i++) {
            if (_ticking[i]) {
                KillTimer(_view->GetHandle(), ID_LAYER_TIMER + i);
                _ticking[i] = false;
            }
        }

        _killText();
    }

    void _killText() {
        if (TextLayer::Text.empty()) {
            return;
        }

        KillTimer(_view->GetHandle(), ID_TEXT_TIMER);
        TextLayer::Text.clear();
    }

public:
    OverlaySurface(MainWindow* view, OverlaySettings& config) : _view(view), _cfg(config) {
        // Last, so what an action says reads after whatever the features contributed
        Overlay::Add({.Id = "overlay.text",
                      .Order = Overlay::Last,
                      .Measure = &TextLayer::Measure,
                      .Render = &TextLayer::Render});
    }

    /// Once the window exists - the same hand-off Feedback takes for its PostFn.
    void Bind() { Overlay::Invalidate = &MainWindow::PostRelayout; }

    /// The one place that decides whether the overlay is on screen and how wide it is.
    void Relayout() {
        // While nothing has widened the strip, where the window is *is* the anchor - that is how
        // dragging it is stored. Reading it back while a pill is up would walk it across the
        // screen notification by notification, because the work-area clamp shifts it left.
        if (_slots.TotalWidth <= _slots.Height) {
            _anchor = {_view->GetPositionX(), _view->GetPositionY()};
        }

        _slots = OverlaySlots::Measure(_cfg.Size, _preview);
        _syncTimers();

        if (!_slots.TotalWidth) {
            // Parked back on the anchor before it goes: RefreshPos writes its work-area clamp
            // into the window position, and a hidden window keeps it - the next pass would read
            // that clamped position back as the anchor and the overlay would stay where a wide
            // pill had pushed it.
            _view->SetPositionX(_anchor.x)->SetPositionY(_anchor.y);
            _view->Hide();
            return;
        }

        // Seeded from the anchor every time, so the work-area clamp inside RefreshPos stays
        // transient instead of walking the overlay across the screen
        _view->SetPositionX(_originX())
             ->SetPositionY(_anchor.y)
             ->SetWidth(_slots.TotalWidth)
             ->SetHeight(_slots.Height);

        _view->Show();
        _view->RefreshPos(HWND_TOPMOST);
        _view->Invalidate();
    }

    void Render(Gdiplus::Graphics& canvas) const { _slots.Render(canvas); }

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

    /// Text that is already resolved, handed over through the window's message queue.
    void ShowText(std::wstring text) {
        TextLayer::Text = std::move(text);
        // Same id, so a second action while the first is still up just restarts the countdown
        SetTimer(_view->GetHandle(), ID_TEXT_TIMER, TextDurationMs, nullptr);
        Relayout();
    }

    /// The settings window is taking over: stop polling, drop the text, and show a preview so
    /// the user can find the overlay to drag it.
    void Suspend() {
        _killTimers();
        _preview = true;

        if (_view->IsOvershadowed()) {
            _view->Hide();
            _view->SetShadowHwnd(nullptr);
        }

        Relayout();
    }

    /// The settings window is gone: put back the window properties the config asks for.
    void Restore() {
        _preview = false;

        if (_cfg.OnTopExclusive && UAC::IsElevated() && !_view->IsOvershadowed()) {
            _view->Hide();
            _view->SetShadowHwnd(UIAccessManager::GetOrCreateWindow(
                ShadowWindowKey, MainWindow::StyleEx, MainWindow::Style));
            _view->RefreshPos(HWND_TOPMOST);
        }

        _applyDisplayAffinity();
        Relayout();
    }
};
