#pragma once
#include <cstdint>
using UINT = unsigned int;
using WORD = uint16_t;
using DWORD = uint32_t;
using LONG = int32_t;
using ULONG_PTR = uintptr_t;
constexpr UINT INPUT_MOUSE = 0, INPUT_KEYBOARD = 1;
struct KEYBDINPUT { WORD wVk, wScan; DWORD dwFlags, time; ULONG_PTR dwExtraInfo; };
struct MOUSEINPUT { LONG dx, dy; DWORD mouseData, dwFlags, time; ULONG_PTR dwExtraInfo; };
struct INPUT { DWORD type; union { MOUSEINPUT mi; KEYBDINPUT ki; }; };
constexpr uint8_t VK_LBUTTON = 1, VK_RBUTTON = 2, VK_MBUTTON = 4, VK_XBUTTON1 = 5, VK_XBUTTON2 = 6;
constexpr uint8_t VK_LCONTROL = 0xA2, VK_RCONTROL = 0xA3, VK_LSHIFT = 0xA0, VK_RSHIFT = 0xA1, VK_LMENU = 0xA4, VK_RMENU = 0xA5;
