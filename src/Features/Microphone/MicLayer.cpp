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
                   || Mic::Audio().CaptureDevice()->AnySessionActive());
    }

    /// Whether a loud peak may turn the pill into "talking" - never over the muted glyph.
    bool _listening() {
        return _settings->Pill == MicPillMode::MutedOrTalk && !Mic::Muted() && _allowed();
    }

    /// Muted, or talking while the debounce still says so - only the peak meter's own tick may
    /// decide the microphone went quiet, so a repaint in the middle of a word keeps the glyph.
    void _refresh() {
        if (!_allowed()) {
            _bitmap = nullptr;
        } else if (Mic::Muted()) {
            _bitmap = _icons->Muted();
        } else {
            _bitmap = _peakPhase ? _icons->Active() : nullptr;
        }
    }

    OverlaySlot _measure(const OverlayCell& cell) {
        // The settings window is open: show the glyph whatever the device says, so the user can
        // see what they are dragging
        if (cell.Preview) {
            return {.Width = cell.Height};
        }

        // Nothing to draw and still polling: this is the cue that the mic went live, and it is why
        // the overlay no longer keeps an empty window up to host a timer
        return {.Width = _bitmap ? cell.Height : 0, .WantsTick = _listening()};
    }

    /// The pill is square, so the granted width says nothing the cell does not.
    void _render(const OverlayCell& cell, Gdiplus::Graphics& canvas, int) {
        Gdiplus::Bitmap* bitmap = cell.Preview ? _icons->Unmuted() : _bitmap;
        const int inset = (cell.Height - cell.IconSize) / 2;
        canvas.DrawImage(bitmap, inset, inset, cell.IconSize, cell.IconSize);
    }

    /// The timer also runs while the muted glyph has width, and then only lets a leftover phase run out.
    void _tick() {
        if (_listening() && Mic::Audio().CaptureDevice()->GetPeak() > _settings->VolumeThreshold) {
            if (!_peakPhase) {
                _bitmap = _icons->Active();
                Overlay::Changed();
            }
            // Every loud tick restarts the count, so only quiet ticks in a row end "talking"
            _peakPhase = PeakDebouncePhases;
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

    void Refresh() {
        _refresh();
    }

    HICON TrayIcon() {
        return _icons->Tray(!Mic::HasDevice() || Mic::Muted());
    }

    void RefreshTheme() {
        _icons->RefreshTheme();
    }
}
