#pragma once

#include <string>
#include <vector>

namespace Console {

    void Init();
    bool ReadLine(std::wstring& out);
    void Print(const std::wstring& s);

    std::vector<std::string> CommandLineUtf8();

}
