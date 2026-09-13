#pragma once

#include <gdiplus.h>
#include <windows.h>

/**
 * @brief Keeps the tray icon readable on a light taskbar.
 *
 * A white glyph disappears there. Instead of a second set of assets the bright pixels are
 * darkened at runtime: an icon that is already coloured sits below the luminance threshold and
 * passes through untouched, so one that means something by its colour keeps meaning it.
 */
namespace TrayIconTheme {

    /// The taskbar follows SystemUsesLightTheme - AppsUseLightTheme is the window theme instead.
    inline bool IsLightTaskbar() {
        DWORD light = 0;
        DWORD size = sizeof(light);

        return RegGetValueW(HKEY_CURRENT_USER,
                            LR"(SOFTWARE\Microsoft\Windows\CurrentVersion\Themes\Personalize)",
                            L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size) == ERROR_SUCCESS
               && light != 0;
    }

    /// True for the WM_SETTINGCHANGE Windows broadcasts when the theme is switched.
    inline bool IsColorSetChange(LPARAM lParam) {
        const auto* section = reinterpret_cast<const wchar_t*>(lParam);
        return section && wcscmp(section, L"ImmersiveColorSet") == 0;
    }

    /// Owned copy with the bright pixels pulled down to near black, or nullptr on failure.
    inline HICON Darken(HICON icon) {
        constexpr int BrightnessThreshold = 200;
        constexpr int DarkLevel = 32;

        if (!icon) {
            return nullptr;
        }

        Gdiplus::Bitmap bitmap(icon);
        const Gdiplus::Rect area(0, 0, static_cast<INT>(bitmap.GetWidth()), static_cast<INT>(bitmap.GetHeight()));

        Gdiplus::BitmapData data;
        if (bitmap.LockBits(&area, Gdiplus::ImageLockModeRead | Gdiplus::ImageLockModeWrite,
                            PixelFormat32bppARGB, &data) != Gdiplus::Ok) {
            return nullptr;
        }

        for (UINT y = 0; y < data.Height; y++) {
            auto* row = static_cast<BYTE*>(data.Scan0) + static_cast<intptr_t>(y) * data.Stride;

            for (UINT x = 0; x < data.Width; x++) {
                BYTE* pixel = row + x * 4; // BGRA, alpha stays as it is so the edges keep their shape
                if ((pixel[2] * 299 + pixel[1] * 587 + pixel[0] * 114) / 1000 < BrightnessThreshold) {
                    continue;
                }

                pixel[0] = static_cast<BYTE>(pixel[0] * DarkLevel / 255);
                pixel[1] = static_cast<BYTE>(pixel[1] * DarkLevel / 255);
                pixel[2] = static_cast<BYTE>(pixel[2] * DarkLevel / 255);
            }
        }

        bitmap.UnlockBits(&data);

        HICON darkened = nullptr;
        return bitmap.GetHICON(&darkened) == Gdiplus::Ok ? darkened : nullptr;
    }
}

