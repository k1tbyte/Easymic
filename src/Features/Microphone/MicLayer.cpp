#include "MicLayer.hpp"

#include <memory>
#include <gdiplus.h>

#include "AppConfig.hpp"
#include "Core/Overlay.hpp"
#include "MicIcons.hpp"
#include "Microphone.hpp"
#include "Wasapi/AudioManager.hpp"

namespace {

    /// Two ticks below the threshold before the pill stops saying "talking", so a gap between
    /// words does not flicker it.
    constexpr int PeakDebouncePhases = 2;
    constexpr uint16_t PeakIntervalMs = 150;

    MicSettings* _settings = nullptr;
    std::unique_ptr<MicIcons> _icons;

    /// What the pill shows, null for nothing at all.
    Gdiplus::Bitmap* _bitmap = nullptr;
    int _peakPhase = 0;

    /// Whether the pill may be on screen at all - config and live sessions, no mute state.
    bool _allowed() {
        return Mic::HasDevice() && _settings->Pill != MicPillMode::Hidden
               && (!_settings->HideWhenInactive
                   || Mic::Audio().CaptureDevice()->GetActiveSessionsCount() > 0);
    }

    /// Muted is the only thing the pill shows by itself - the peak meter overrides it while
    /// talking.
    void _refresh() {
        _bitmap = (_allowed() && Mic::Muted()) ? _icons->Muted() : nullptr;
    }

    OverlaySlot _measure(const OverlayCell& cell) {
        // The settings window is open: show the glyph whatever the device says, so the user can
        // see what they are dragging
        if (cell.Preview) {
            return {.Width = cell.Height};
        }

        return {.Width = _bitmap ? cell.Height : 0,
                // Nothing to draw and still polling: this is the cue that the mic went live, and
                // it is why the overlay no longer keeps an empty window up to host a timer
                .WantsTick = _settings->Pill == MicPillMode::MutedOrTalk && !Mic::Muted()
                             && _allowed()};
    }

    /// The pill is square, so the granted width says nothing the cell does not.
    void _render(const OverlayCell& cell, Gdiplus::Graphics& canvas, int) {
        Gdiplus::Bitmap* bitmap = cell.Preview ? _icons->Unmuted() : _bitmap;
        const int inset = (cell.Height - cell.IconSize) / 2;
        canvas.DrawImage(bitmap, inset, inset, cell.IconSize, cell.IconSize);
    }

    void _tick() {
        if (Mic::Audio().CaptureDevice()->GetPeak() > _settings->VolumeThreshold) {
            if (!_peakPhase) {
                _bitmap = _icons->Active();
                _peakPhase = PeakDebouncePhases;
                Overlay::Changed();
            }
            return;
        }

        if (_peakPhase && --_peakPhase == 0) {
            _refresh();
            Overlay::Changed();
        }
    }

} // anonymous namespace

namespace MicLayer {

    void Register(const HINSTANCE instance, MicSettings& settings) {
        _settings = &settings;
        _icons = std::make_unique<MicIcons>(instance);

        // The peak phase is left alone on purpose: a state change repaints the pill, it does not
        // decide that the microphone stopped being loud
        Mic::OnStateChanged += [] {
            _refresh();
            Overlay::Changed();
        };

        Overlay::Add({.Id = "mic.pill",
                      .Measure = &_measure,
                      .Render = &_render,
                      .TickMs = PeakIntervalMs,
                      .Tick = &_tick});
    }

    HICON TrayIcon() {
        return _icons->Tray(!Mic::HasDevice() || Mic::Muted());
    }

    void RefreshTheme() {
        _icons->RefreshTheme();
    }
}
