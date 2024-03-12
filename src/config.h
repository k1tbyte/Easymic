#ifndef EASYMIC_CONFIG_H
#define EASYMIC_CONFIG_H

#include <windows.h>
#include <cstdio>
#include <sys/stat.h>
#include <string>

#define ConfigName "conf.bin"

struct Config {

    BYTE indicatorSize = 16;
    USHORT windowPosX = 0;
    USHORT windowPosY = 0;

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
