#pragma once

#include <memory>
#include <gdiplus.h>
#include <windows.h>

#include "TrayIconTheme.hpp"
#include "Resources/Resource.h"

/**
 * @brief The glyphs the indicator and the tray share, and who owns them.
 *
 * The pill always draws the bright originals - it has its own dark background. Only the tray
 * needs darkened copies, and only on a light taskbar, so they are built on the first switch to
 * one and kept from then on.
 */
class IndicatorIcons {
    // Shared icons from LoadIcon - never DestroyIcon'd
    HICON _muted = nullptr;
    HICON _unmuted = nullptr;

    // Darkened copies for a light taskbar - owned, unlike the shared originals above
    HICON _mutedDark = nullptr;
    HICON _unmutedDark = nullptr;
    bool _isTaskbarLight = false;

    // The HICON -> Bitmap conversion is expensive, and only these three are ever drawn
    std::unique_ptr<Gdiplus::Bitmap> _mutedBitmap;
    std::unique_ptr<Gdiplus::Bitmap> _unmutedBitmap;
    std::unique_ptr<Gdiplus::Bitmap> _activeBitmap;

public:
    explicit IndicatorIcons(HINSTANCE hInstance) {
        _muted = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MIC_MUTED));
        _unmuted = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MIC_UNMUTED));
        const HICON active = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MIC_ACTIVE));

        _mutedBitmap = std::make_unique<Gdiplus::Bitmap>(_muted);
        _unmutedBitmap = std::make_unique<Gdiplus::Bitmap>(_unmuted);
        _activeBitmap = std::make_unique<Gdiplus::Bitmap>(active);

        RefreshTheme();
    }

    ~IndicatorIcons() {
        // Only the darkened copies are ours - the originals come from LoadIcon and are shared
        if (_mutedDark) {
            DestroyIcon(_mutedDark);
        }
        if (_unmutedDark) {
            DestroyIcon(_unmutedDark);
        }
    }

    IndicatorIcons(const IndicatorIcons&) = delete;
    IndicatorIcons& operator=(const IndicatorIcons&) = delete;

    /// Darkened copies are built once, on the first switch to a light taskbar.
    void RefreshTheme() {
        _isTaskbarLight = TrayIconTheme::IsLightTaskbar();

        if (_isTaskbarLight && !_mutedDark) {
            _mutedDark = TrayIconTheme::Darken(_muted);
            _unmutedDark = TrayIconTheme::Darken(_unmuted);
        }
    }

    /// What the tray should show, already themed for the current taskbar.
    HICON Tray(const bool muted) const {
        if (_isTaskbarLight) {
            if (HICON darkened = muted ? _mutedDark : _unmutedDark) {
                return darkened;
            }
        }
        return muted ? _muted : _unmuted;
    }

    Gdiplus::Bitmap* Muted() const { return _mutedBitmap.get(); }
    Gdiplus::Bitmap* Unmuted() const { return _unmutedBitmap.get(); }
    Gdiplus::Bitmap* Active() const { return _activeBitmap.get(); }
};

