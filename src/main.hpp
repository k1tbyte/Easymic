#ifndef EASYMIC_MAIN_HPP
#define EASYMIC_MAIN_HPP

#include <windows.h>
#include <cstdio>
#include <sys/stat.h>
#include <string>

#define	WM_USER_SHELLICON (WM_USER + 1)
#define WM_TASKBAR_CREATE RegisterWindowMessage(_T("TaskbarCreated"))
#define ConfigName "conf.bin"
#define MutexName "Easymic-8963D562-E35B-492A-A3D2-5FD724CE24B1"
#define UID 565746541
#define AutoStartupHKEY R"(SOFTWARE\Microsoft\Windows\CurrentVersion\Run)"
#define AppName "Easymic"

struct Config {

    WORD  keybdHotkey{};
    WORD  mouseHotkeyIndex{};
    bool  isMouseHotkeyMode = false;

    [[nodiscard]] bool Equals(Config& config) const
    {
        return config.isMouseHotkeyMode == this->isMouseHotkeyMode &&
               config.keybdHotkey == this->keybdHotkey &&
               config.mouseHotkeyIndex == this->mouseHotkeyIndex;
    }

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

struct Hotkey {
    const char* Name;
    int VK;
    int WM;
};

#endif //EASYMIC_MAIN_HPP
