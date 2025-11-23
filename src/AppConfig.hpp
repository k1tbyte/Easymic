#ifndef EASYMIC_CONFIG_HPP
#define EASYMIC_CONFIG_HPP
#define CONFIG_ENABLED

#include <cstdint>
#include <sys/stat.h>
#include <string>
#include <unordered_map>
#include <set>
#include <windows.h>
#include "definitions.h"

#ifdef CONFIG_ENABLED
#include <glaze/glaze.hpp>
#endif // CONFIG_ENABLED

enum class IndicatorState {
    Hidden,
    Muted,
    MutedOrTalk,
};

struct AppConfig {

    static inline std::string DefaultPath{};

    uint16_t WindowPosX            = 0;
    uint16_t WindowPosY            = 0;
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
    std::set<std::string> SkippedVersions;
    std::unordered_map<std::string , uint64_t> Hotkeys;
    std::set<std::string> UnmuteSoundRecentSources;
    std::set<std::string> MuteSoundRecentSources;
    std::set<std::string> UnmuteIconRecentSources;
    std::set<std::string> MuteIconRecentSources;
    std::string MuteSoundSource;
    std::string UnmuteSoundSource;
    std::string MutedIconSource;
    std::string UnmutedIconSource;

    bool operator==(const AppConfig&) const = default;

    static void Save(const AppConfig& config)
    {
#ifdef CONFIG_ENABLED
        glz::write_file_beve(config, GetConfigPath(), std::string{});
#endif // CONFIG_ENABLED
    }

    void Save() const {
        Save(*this);
    }

    static AppConfig Load()
    {
        AppConfig config{};
#ifdef CONFIG_ENABLED
        glz::read_file_beve(config, GetConfigPath(), std::string{});
#endif // CONFIG_ENABLED
        return config;
    }

private:
    static std::string GetConfigPath()
    {

#ifdef CONFIG_ENABLED
        if (DefaultPath.empty()) {
            wchar_t modulePath[MAX_PATH];
            GetModuleFileNameW(nullptr, modulePath, MAX_PATH);

            const std::filesystem::path path{modulePath};
            DefaultPath = (path.parent_path() / CONFIG_NAME).string();
        }
#endif // CONFIG_ENABLED
        return DefaultPath;
    }
};

#endif //EASYMIC_CONFIG_HPP
