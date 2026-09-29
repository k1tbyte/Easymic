#pragma once

#include <memory>
#include <gdiplus.h>
#include <windows.h>

#include "Gfx/TrayIconTheme.hpp"
#include "Resources/Resource.h"

/// The pill draws the bright originals on its own dark background; only a light taskbar tray needs darkened copies.
class MicIcons {
    HICON _muted = nullptr;
    HICON _unmuted = nullptr;

    // Owned, unlike the shared LoadIcon originals above
    HICON _mutedDark = nullptr;
    HICON _unmutedDark = nullptr;
    bool _isTaskbarLight = false;

    std::unique_ptr<Gdiplus::Bitmap> _mutedBitmap;
    std::unique_ptr<Gdiplus::Bitmap> _unmutedBitmap;
    std::unique_ptr<Gdiplus::Bitmap> _activeBitmap;

public:
    explicit MicIcons(HINSTANCE hInstance) {
        _muted = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MIC_MUTED));
        _unmuted = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MIC_UNMUTED));
        const HICON active = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MIC_ACTIVE));

        _mutedBitmap = std::make_unique<Gdiplus::Bitmap>(_muted);
        _unmutedBitmap = std::make_unique<Gdiplus::Bitmap>(_unmuted);
        _activeBitmap = std::make_unique<Gdiplus::Bitmap>(active);

        RefreshTheme();
    }

    ~MicIcons() {
        if (_mutedDark) {
            DestroyIcon(_mutedDark);
        }
        if (_unmutedDark) {
            DestroyIcon(_unmutedDark);
        }
    }

    MicIcons(const MicIcons&) = delete;
    MicIcons& operator=(const MicIcons&) = delete;

    void RefreshTheme() {
        _isTaskbarLight = TrayIconTheme::IsLightTaskbar();

        if (_isTaskbarLight && !_mutedDark) {
            _mutedDark = TrayIconTheme::Darken(_muted);
            _unmutedDark = TrayIconTheme::Darken(_unmuted);
        }
    }

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

