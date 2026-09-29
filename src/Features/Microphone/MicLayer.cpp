#include "MicLayer.hpp"

#include <memory>
#include <gdiplus.h>

#include "AppConfig.hpp"
#include "Core/Overlay.hpp"
#include "MicIcons.hpp"
#include "Microphone.hpp"
#include "Wasapi/AudioManager.hpp"

namespace {

    /// Quiet ticks in a row before "talking" ends, so a gap between words does not flicker.
    constexpr int PeakDebouncePhases = 2;
    constexpr uint16_t PeakIntervalMs = 150;

    MicSettings* _settings = nullptr;
    std::unique_ptr<MicIcons> _icons;

    Gdiplus::Bitmap* _bitmap = nullptr;
    int _peakPhase = 0;

    bool _allowed() {
        return Mic::HasDevice() && _settings->Pill != MicPillMode::Hidden
               && (!_settings->HideWhenInactive
                   || Mic::Audio().CaptureDevice()->AnySessionActive());
    }

    bool _listening() {
        return _settings->Pill == MicPillMode::MutedOrTalk && !Mic::Muted() && _allowed();
    }

    /// Only the tick may end "talking": a repaint in the middle of a word keeps the glyph.
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
        if (cell.Preview) {
            return {.Width = cell.Height};
        }

        return {.Width = _bitmap ? cell.Height : 0, .WantsTick = _listening() || _peakPhase};
    }

    void _render(const OverlayCell& cell, Gdiplus::Graphics& canvas, int) {
        Gdiplus::Bitmap* bitmap = cell.Preview ? _icons->Unmuted() : _bitmap;
        const int inset = (cell.Height - cell.IconSize) / 2;
        canvas.DrawImage(bitmap, inset, inset, cell.IconSize, cell.IconSize);
    }

    void _tick() {
        if (_listening() && Mic::Audio().CaptureDevice()->GetPeak() > _settings->VolumeThreshold) {
            if (!_peakPhase) {
                _bitmap = _icons->Active();
                Overlay::Changed();
            }
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
