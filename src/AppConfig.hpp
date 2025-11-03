#ifndef EASYMIC_CONFIG_HPP
#define EASYMIC_CONFIG_HPP


#include <sys/stat.h>
#include <string>
#include <windows.h>

#define MutexName L"Easymic-8963D562-E35B-492A-A3D2-5FD724CE24B2"
#define AppName L"Easymic"
#define ConfigName L"conf.bin"

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

    USHORT windowPosX        = 0;
    USHORT windowPosY        = 0;
    DWORD muteHotkey         = 0;
    BYTE bellVolume          = 50;
    BYTE micVolume           = -1;
    IndicatorState indicator = IndicatorState::Hidden;
    BYTE indicatorSize       = 16;
    float volumeThreshold    = .01f;
    bool excludeFromCapture  = false;
    bool onTopExclusive      = false;

    bool operator==(const AppConfig&) const = default;

    static void Save(AppConfig* config, const wchar_t* path)
    {
        FILE* file = _wfopen(path, L"wb");
        if (file) {
            fwrite(config, sizeof(AppConfig), 1, file);
            fclose(file);
        }
    }

    static void Load(AppConfig* config, const wchar_t* path)
    {
        struct _stat buffer{};

        if(_wstat(path, &buffer) == 0) {
            FILE* file = _wfopen(path, L"rb");
            if (file) {
                fread(config, sizeof(AppConfig), 1, file);
                fclose(file);
            }
            return;
        }

        *config = AppConfig();
    }

    static const wchar_t* GetConfigPath()
    {
        static wchar_t configPath[MAX_PATH];

        GetModuleFileNameW(nullptr, configPath, MAX_PATH);

        const auto pos = std::wstring(configPath).find_last_of(L"\\/");
        const auto path = std::wstring(configPath).substr(0, pos).append(L"\\").append(ConfigName);

        wcscpy(configPath, path.c_str());

        return configPath;
    }
};

#endif //EASYMIC_CONFIG_HPP
