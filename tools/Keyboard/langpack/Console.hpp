#pragma once

#include <string>
#include <vector>

namespace Console {

    void Init();
    bool ReadLine(std::wstring& out);
    void Print(const std::wstring& s);

    std::wstring FromUtf8(const std::string& s);
    std::string ToUtf8(const std::wstring& s);
    std::wstring ReadFileUtf8(const std::string& path);

    std::vector<std::string> CommandLineUtf8();

}
