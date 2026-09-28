#include "Console.hpp"

#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <fstream>
#include <iostream>
#include <vector>

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

std::wstring FromUtf8(const std::string& s) {
    if (s.empty()) {
        return {};
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

std::string ToUtf8(const std::wstring& s) {
    if (s.empty()) {
        return {};
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0,
                                      nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr,
                        nullptr);
    return out;
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
        out = FromUtf8(line);
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
        const std::string utf8 = ToUtf8(s);
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
        out.push_back(ToUtf8(parts[i]));
    }
    LocalFree(parts);
    return out;
}

std::wstring ReadFileUtf8(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return {};
    }
    std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF) {
        bytes.erase(0, 3);
    }
    return FromUtf8(bytes);
}

}
