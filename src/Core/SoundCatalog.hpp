#pragma once

#include <mmsystem.h>
#include <mutex>
#include <string>
#include <windows.h>

#include "Resources/Resource.h"
#include "Str.hpp"

namespace SoundCatalog {

    struct Bundled {
        const char* Key;
        const char* Title;
        int ResourceId;
    };

    inline constexpr Bundled All[] = {
        {"Mute",    "Discord mute",   IDR_MUTE},
        {"Unmute",  "Discord unmute", IDR_UNMUTE},
        {"Tick",    "Tick",           IDR_TICK},
        {"Enable",  "Enable",         IDR_ENABLE},
        {"Disable", "Disable",        IDR_DISABLE},
    };

    inline const Bundled* Find(const std::string& sound) {
        for (const auto& entry : All) {
            if (sound == entry.Key) {
                return &entry;
            }
        }
        return nullptr;
    }

    /// The level is process-wide: the lock stops the hotkey worker and device notifications playing at each other's.
    inline void Play(HINSTANCE hInstance, const std::string& sound, const uint8_t volumePercent) {
        if (sound.empty() || !volumePercent) {
            return;
        }

        static std::mutex mutex;
        const std::lock_guard lock(mutex);

        const auto level = static_cast<WORD>((volumePercent > 100 ? 100 : volumePercent) * 0xFFFF / 100);
        waveOutSetVolume(nullptr, MAKELONG(level, level));

        if (const Bundled* bundled = Find(sound)) {
            PlaySoundW(MAKEINTRESOURCEW(bundled->ResourceId), hInstance, SND_ASYNC | SND_RESOURCE);
            return;
        }

        PlaySoundW(Str::Utf8ToWide(sound).c_str(), nullptr, SND_ASYNC | SND_FILENAME | SND_NODEFAULT);
    }
}

