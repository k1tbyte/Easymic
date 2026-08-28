#ifndef EASYMIC_CONFIG_HPP
#define EASYMIC_CONFIG_HPP
#define CONFIG_ENABLED

#include <cstdint>
#include <sys/stat.h>
#include <string>
#include <vector>
#include <set>
#include <windows.h>
#include "Actions.hpp"
#include "definitions.h"

#ifdef CONFIG_ENABLED
#include <glaze/glaze.hpp>
#endif // CONFIG_ENABLED

enum class IndicatorState {
    Hidden,
    Muted,
    MutedOrTalk,
};

/**
 * @brief One thing a hotkey does, whether that is a built-in or a command line.
 *
 * Built-ins are entries like any other rather than a fixed table, so the same one can sit in the
 * list as many times as the user has keys for it - three language switches over different locale
 * rings is the case that asked for this.
 */
struct Action {
    std::string Name;
    /// BuiltInAction::Key, empty when this runs the command line below instead.
    std::string BuiltIn;
    std::string Command;
    /// What the built-in was configured with. Only the language switch reads one so far, as a
    /// comma separated list of locales.
    std::string Args;
    /// SoundCatalog key or a file path, empty means silent.
    std::string Sound;
    /// Overlay text shown when the action fires, {token} aware. Empty falls back to the built-in
    /// table, so a config written before a default existed still gets one.
    std::string Notification;
    uint64_t Hotkey = 0;
    bool OnRelease  = false;
    /// How many times the combination is pressed in a row to run this. One combination can drive
    /// several actions as long as the count differs.
    uint8_t Presses = 1;
    /// Swallows the combination instead of passing it on, so the app underneath never sees it.
    bool Block = false;
    /// Release only: skips the action when another key was pressed while this one was held, which
    /// is what lets the same key double as a modifier.
    bool TapOnly = false;
    /// How loud this action plays its sound, 0-100. Independent of BellVolume, which belongs to
    /// the mic state chime alone.
    uint8_t SoundVolume = 100;
    bool ShowNotification = false;

    bool operator==(const Action&) const = default;
};

struct AppConfig {

    static inline std::string DefaultPath{};

    int32_t WindowPosX             = 0;
    int32_t WindowPosY             = 0;
    /// The mic state chime only - action sounds carry their own level.
    int8_t BellVolume              = 25;
    int8_t MicVolume               = -1;
    uint8_t IndicatorSize          = 16;
    IndicatorState IndicatorState  = IndicatorState::Muted;
    float IndicatorVolumeThreshold = .001f;
    bool ExcludeFromCapture        = false;
    bool OnTopExclusive            = false;
    bool IsMicKeepVolume           = true;
    bool IsUpdatesEnabled          = true;
    bool IsAutoUpdateEnabled       = false;
    bool IsSkipUACEnabled          = false;
    bool HideWhenInactive          = true;
    /// Master switch for the on-screen action notifications; per action, an empty text is the off.
    bool NotificationsEnabled      = true;
    /// How long a combination waits for another press before it decides how many there were.
    uint16_t MultiPressWindowMs    = 200;
    /// Which build's shape the file was written in. 0 is one that predates the field, so it must
    /// not default to the current revision - the whole point is telling those apart.
    int32_t Version = 0;

    std::set<std::string> SkippedVersions;
    /// Every action in the order the settings list shows them.
    std::vector<Action> Actions;
    /// One list behind every sound picker, whatever the picker is for.
    std::set<std::string> RecentSounds;
    /// Mic state feedback, not an action sound: these play whenever the device changes state,
    /// including a mute from Discord or the mixer. SoundCatalog keys or file paths.
    std::string MuteSoundSource = "Mute";
    std::string UnmuteSoundSource = "Unmute";

    bool operator==(const AppConfig&) const = default;

    static void Save(const AppConfig& config)
    {
#ifdef CONFIG_ENABLED
        if (const auto ec = glz::write_file_beve(config, GetConfigPath(), std::string{})) {
            LOG_ERROR("Config save failed: %s", glz::format_error(ec).c_str());
        }
#endif // CONFIG_ENABLED
    }

    void Save() const {
        Save(*this);
    }

    static AppConfig Load()
    {
        AppConfig config{};
#ifdef CONFIG_ENABLED
        // Unknown keys are dropped on purpose: removing a setting must not invalidate the file.
        // A missing file is the normal first-run case, anything else means a broken config.
        if (const auto ec = glz::read_file_beve<glz::opts{.error_on_unknown_keys = false}>(
                config, GetConfigPath(), std::string{});
            ec && ec.ec != glz::error_code::file_open_failure) {
            LOG_ERROR("Config load failed: %s", glz::format_error(ec).c_str());
        }

        // Stamped on the way in rather than written by whoever saves, so the number always says
        // which build shaped the file - there is nothing to migrate yet, and that is the anchor
        // the first migration will need
        config.Version = CurrentVersion;
#endif // CONFIG_ENABLED
        return config;
    }

private:
    static constexpr int32_t CurrentVersion = 2;

    static std::string GetConfigPath()
    {

#ifdef CONFIG_ENABLED
        if (DefaultPath.empty()) {
            wchar_t modulePath[MAX_PATH] = {};
            if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) == 0) {
                DefaultPath = "conf.b";
            } else {
                const std::filesystem::path path{modulePath};
                DefaultPath = (path.parent_path() / CONFIG_NAME).string();
            }
        }
#endif // CONFIG_ENABLED
        return DefaultPath;
    }
};

#endif //EASYMIC_CONFIG_HPP
