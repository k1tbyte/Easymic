#ifndef EASYMIC_SOUNDCATALOG_HPP
#define EASYMIC_SOUNDCATALOG_HPP

#include <mmsystem.h>
#include <string>
#include <windows.h>

#include "Resource.hpp"
#include "Resources/Resource.h"

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

    /// Plays a bundled key or a user file. Empty is silence, a missing file is silence too.
    inline void Play(HINSTANCE hInstance, const std::string& sound) {
        if (sound.empty()) {
            return;
        }

        if (const Bundled* bundled = Find(sound)) {
            // The buffer points into the module image, so it outlives the async playback
            const Resource resource = Resource::FromModule(hInstance, MAKEINTRESOURCEA(bundled->ResourceId), "WAVE");
            if (!resource.empty()) {
                PlaySoundA(reinterpret_cast<LPCSTR>(resource.buffer()), nullptr, SND_ASYNC | SND_MEMORY);
            }
            return;
        }

        PlaySoundA(sound.c_str(), nullptr, SND_ASYNC | SND_FILENAME | SND_NODEFAULT);
    }
}

#endif //EASYMIC_SOUNDCATALOG_HPP
