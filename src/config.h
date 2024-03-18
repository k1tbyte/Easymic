#ifndef EASYMIC_CONFIG_H
#define EASYMIC_CONFIG_H

#include <windows.h>
#include <cstdio>
#include <sys/stat.h>
#include <string>

#define MutexName "Easymic-8963D562-E35B-492A-A3D2-5FD724CE24B1"
#define AppName L"Easymic"
#define ConfigName "conf.bin"

#define WM_SHELLICON (WM_USER + 1)
#define WM_UPDATE_MIC (WM_USER + 2)
#define MIC_PEAK_TIMER (WM_USER + 3)
#define WM_SWITCH_STATE (WM_USER + 4)

enum class IndicatorState {
    Hidden,
    Muted,
    MutedOrTalk,
    Always,
    AlwaysAndTalk
};

enum class IndicatorSize {
    Tiny = 16,
    Large = 32
};

struct Config {

    USHORT windowPosX = 0;
    USHORT windowPosY = 0;
    DWORD muteHotkey = 0;
    BYTE bellVolume = 50;
    BYTE micVolume = -1;
    bool muteZeroMode = false;
    IndicatorState indicator = IndicatorState::Hidden;
    BYTE indicatorSize = 16;
    float volumeThreshold = .01f;

    bool operator==(const Config&) const = default;

    static void Save(Config* config, const char* path)
    {
        FILE* file = fopen(path, "wb");
        fwrite(config, sizeof(Config), 1,file);
        fclose(file);
    }

    static void Load(Config* config, const char* path)
    {
        struct stat   buffer{};

        if(stat(path, &buffer) == 0) {
            FILE* file = fopen(path, "rb");
            fread(config, sizeof(Config), 1,file);
            fclose(file);
        }

        config = new Config();
    }

    static const char* GetConfigPath()
    {
        char buffer[MAX_PATH];

        GetModuleFileName(nullptr, buffer, MAX_PATH);

        const auto pos = std::string(buffer).find_last_of("\\/");
        const auto path =std::string(buffer).substr(0, pos).append("\\").append(ConfigName);
        char* configPath = new char[path.length()];
        strcpy(configPath,path.c_str());
        return configPath;
    }
};

#endif //EASYMIC_CONFIG_H
