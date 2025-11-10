#ifndef EASYMIC_CONFIG_HPP
#define EASYMIC_CONFIG_HPP


#include <cstdint>
#include <sys/stat.h>
#include <string>
#include <unordered_map>
#include <set>
#include <windows.h>
/*#include <glaze/glaze.hpp>*/

#define MutexName L"Easymic-8963D562-E35B-492A-A3D2-5FD724CE24B2"
#define AppName L"Easymic"
#define ConfigName L"conf.b"

#define WM_SHELLICON (WM_USER + 1)
#define WM_UPDATE_MIC (WM_USER + 2)
#define MIC_PEAK_TIMER (WM_USER + 3)
#define WM_UPDATE_STATE (WM_USER + 4)

enum class IndicatorState {
    Hidden,
    Muted,
    MutedOrTalk,
};

struct AppConfig {

    static inline std::string DefaultPath{};

    uint16_t WindowPosX            = 0;
    uint16_t WindowPosY            = 0;
    uint8_t BellVolume             = 50;
    uint8_t MicVolume              = -1;
    uint8_t IndicatorSize          = 16;
    IndicatorState IndicatorState  = IndicatorState::Hidden;
    float IndicatorVolumeThreshold = .01f;
    bool ExcludeFromCapture        = false;
    bool OnTopExclusive            = false;
    std::unordered_map<std::string , uint64_t> Hotkeys;
    std::set<std::string> RecentSoundSources;
    std::set<std::string> RecentIconSources;
    std::string MuteSoundSource;
    std::string UnmuteSoundSource;
    std::string MutedIconSource;
    std::string UnmutedIconSource;

    bool operator==(const AppConfig&) const = default;

    static void Save(const AppConfig& config)
    {
       // glz::write_file_beve(config, GetConfigPath(), std::string{});
    }

    void Save() const {
        Save(*this);
    }

    static AppConfig Load()
    {
        AppConfig config{};
      //  glz::read_file_beve(config, GetConfigPath(), std::string{});
        return config;
    }

private:
    static std::string GetConfigPath()
    {

        /*if (DefaultPath.empty()) {
            wchar_t modulePath[MAX_PATH];
            GetModuleFileNameW(nullptr, modulePath, MAX_PATH);

            const std::filesystem::path path{modulePath};
            DefaultPath = (path.parent_path() / ConfigName).string();
        }*/
        return DefaultPath;
    }
};

#endif //EASYMIC_CONFIG_HPP
