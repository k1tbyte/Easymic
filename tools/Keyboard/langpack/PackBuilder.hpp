#pragma once

#include <filesystem>
#include <string>

#include "Features/Keyboard/Convert/Pack.hpp"

struct PackOptions {
    std::string Locale;
    double Threshold = Convert::DefaultThreshold;
    double FalsePositiveRate = 0.001;
};

bool BuildPack(const std::filesystem::path& wordList, const PackOptions& options,
               const std::filesystem::path& out, std::string* error);
