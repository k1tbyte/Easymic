#pragma once

#include <string>

struct PackOptions {
    std::string Locale;
    double Threshold = 0.35;
    double FalsePositiveRate = 0.001;
};

bool BuildPack(const std::string& wordListPath, const PackOptions& options,
               const std::string& outPath, std::string* error);
