#pragma once

#include <mmsystem.h>
#include <mutex>
#include <string>
#include <windows.h>

#include "Resource.hpp"
#include "Resources/Resource.h"
#include "Str.hpp"

/**
 * @brief The single registry every sound picker and every playback site goes through.
 *
 * A stored sound is either a bundled key from the table below or a path to a user file, and an
 * empty string means silence. Nothing hardcodes a sound per feature any more: the defaults are
 * just table keys that the user can override or swap.
 */
namespace SoundCatalog {

    struct Bundled {
        const char* Key;    // stored in the config, must stay stable
        const char* Title;  // shown in the pickers
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

    /**
     * @brief Plays a bundled key or a user file at the given level, 0-100.
     *
     * Empty is silence, a missing file is silence too, and so is a level of zero.
     *
     * The level belongs to the whole process, but PlaySound only ever plays one sound at a time,
     * so setting it for the sound about to start is the same as setting it per sound. The lock is
     * what keeps the two playback threads - the hotkey worker and the device notifications - from
     * playing at each other's level.
     */
    inline void Play(HINSTANCE hInstance, const std::string& sound, const uint8_t volumePercent) {
        if (sound.empty() || !volumePercent) {
            return;
        }

        static std::mutex mutex;
        const std::lock_guard lock(mutex);

        const auto level = static_cast<WORD>((volumePercent > 100 ? 100 : volumePercent) * 0xFFFF / 100);
        waveOutSetVolume(nullptr, MAKELONG(level, level));

        if (const Bundled* bundled = Find(sound)) {
            // The buffer points into the module image, so it outlives the async playback
            const Resource resource = Resource::FromModule(hInstance, MAKEINTRESOURCEA(bundled->ResourceId), "WAVE");
            if (!resource.empty()) {
                // SND_MEMORY takes a buffer, not a string - the W entry point is the same call
                PlaySoundW(reinterpret_cast<LPCWSTR>(resource.buffer()), nullptr, SND_ASYNC | SND_MEMORY);
            }
            return;
        }

        PlaySoundW(Str::Utf8ToWide(sound).c_str(), nullptr, SND_ASYNC | SND_FILENAME | SND_NODEFAULT);
    }
}

