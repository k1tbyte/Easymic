#ifndef EASYMIC_MAIN_HPP
#define EASYMIC_MAIN_HPP

#include <windows.h>
#include <cstdio>
#include <sys/stat.h>
#include <string>
#include "Utils.hpp"


#define	WM_USER_SHELLICON (WM_USER + 1)
#define WM_TASKBAR_CREATE RegisterWindowMessage(_T("TaskbarCreated"))
#define HKModifier(h) ((HIBYTE (h) & 2) | ((HIBYTE (h) & 4) >> 2) | ((HIBYTE (h) & 1) << 2))

#define MutexName "Easymic-8963D562-E35B-492A-A3D2-5FD724CE24B1"
#define UID 565746541

#define AppName L"Easymic"



struct Hotkey {
    const char* Name;
    int VK;
    int WM;
};

struct Resource {
    BYTE* buffer;
    DWORD fileSize;
};

#endif //EASYMIC_MAIN_HPP
