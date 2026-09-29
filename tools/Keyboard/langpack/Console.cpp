#include "Console.hpp"

#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <iostream>
#include <vector>

#include "Platform/Str.hpp"

namespace Console {

namespace {

    HANDLE _consoleIn() {
        const HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
        DWORD mode = 0;
        return GetConsoleMode(h, &mode) ? h : nullptr;
    }

    HANDLE _consoleOut() {
        const HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        return GetConsoleMode(h, &mode) ? h : nullptr;
    }

} // anonymous namespace

void Init() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

bool ReadLine(std::wstring& out) {
    const HANDLE h = _consoleIn();
    if (!h) {
        std::string line;
        if (!std::getline(std::cin, line)) {
            return false;
        }
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        out = Str::Utf8ToWide(line);
        return true;
    }

    out.clear();
    std::vector<wchar_t> buf(4096);
    DWORD read = 0;
    if (!ReadConsoleW(h, buf.data(), static_cast<DWORD>(buf.size()), &read, nullptr) || read == 0) {
        return false;
    }
    out.assign(buf.data(), read);
    while (!out.empty() && (out.back() == L'\n' || out.back() == L'\r')) {
        out.pop_back();
    }
    return true;
}

void Print(const std::wstring& s) {
    const HANDLE h = _consoleOut();
    if (h) {
        DWORD written = 0;
        WriteConsoleW(h, s.data(), static_cast<DWORD>(s.size()), &written, nullptr);
    } else {
        const std::string utf8 = Str::WideToUtf8(s);
        std::fwrite(utf8.data(), 1, utf8.size(), stdout);
        std::fflush(stdout);
    }
}

std::vector<std::string> CommandLineUtf8() {
    int count = 0;
    LPWSTR* parts = CommandLineToArgvW(GetCommandLineW(), &count);
    std::vector<std::string> out;
    if (!parts) {
        return out;
    }
    for (int i = 0; i < count; ++i) {
        out.push_back(Str::WideToUtf8(parts[i]));
    }
    LocalFree(parts);
    return out;
}

}
