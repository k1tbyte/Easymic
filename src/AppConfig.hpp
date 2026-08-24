#ifndef EASYMIC_CONFIG_HPP
#define EASYMIC_CONFIG_HPP
#define CONFIG_ENABLED

#include <cstdint>
#include <sys/stat.h>
#include <string>
#include <unordered_map>
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
 * @brief What the user can tune for a built-in action.
 * Custom actions carry the same three fields plus their name and command line.
 */
struct ActionBinding {
    uint64_t Hotkey = 0;
    bool OnRelease  = false;
    /// SoundCatalog key or a file path, empty means silent.
    std::string Sound;

    bool operator==(const ActionBinding&) const = default;
};

/// User defined action: a shell command bound to a hotkey.
struct CustomAction {
    std::string Name;
    std::string Command;
    std::string Sound;
    uint64_t Hotkey  = 0;
    bool OnRelease   = false;

    bool operator==(const CustomAction&) const = default;
};

struct AppConfig {

    static inline std::string DefaultPath{};

    int32_t WindowPosX             = 0;
    int32_t WindowPosY             = 0;
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
    /// Bumped by Load once the migration for that revision has run. 0 means "written before
    /// migrations existed", which is why it must not default to the current revision.
    int32_t Version = 0;

    std::set<std::string> SkippedVersions;
    /// Built-in action id -> its binding. BuiltInActions::All is the list of valid ids.
    std::unordered_map<std::string, ActionBinding> Actions;
    std::vector<CustomAction> CustomActions;
    /// One list behind every sound picker, whatever the picker is for.
    std::set<std::string> RecentSounds;
    /// Mic state feedback, not an action sound: these play whenever the device changes state,
    /// including a mute from Discord or the mixer. SoundCatalog keys or file paths.
    std::string MuteSoundSource = "Mute";
    std::string UnmuteSoundSource = "Unmute";

    /// Superseded by Actions, kept so an old config does not lose its hotkeys on update.
    std::unordered_map<std::string, uint64_t> Hotkeys;

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

        config.Migrate();
#endif // CONFIG_ENABLED
        return config;
    }

private:
    static constexpr int32_t CurrentVersion = 1;

    /// Brings a config written by an older build up to CurrentVersion.
    void Migrate()
    {
        if (Version < 1) {
            // Hotkeys only carried the key mask - the rest comes from the built-in table, so a
            // migrated action ends up exactly where a freshly configured one would
            for (const auto& [id, mask] : Hotkeys) {
                const BuiltInAction* action = BuiltInActions::Find(id);
                if (mask && action && !Actions.contains(id)) {
                    Actions[id] = {.Hotkey = mask, .Sound = action->DefaultSound};
                }
            }
            Hotkeys.clear();
        }

        Version = CurrentVersion;
    }

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
