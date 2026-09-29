#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "Platform/Windowing/Foreground.hpp"

namespace KeyboardExclusions {

    inline std::vector<std::string> Parse(std::string_view csv) {
        std::vector<std::string> apps;
        for (size_t start = 0; start < csv.size();) {
            const size_t comma = csv.find(',', start);
            std::string exe = Foreground::CanonicalApp(csv.substr(start, comma - start));
            if (!exe.empty()) {
                apps.push_back(std::move(exe));
            }
            if (comma == std::string_view::npos) {
                break;
            }
            start = comma + 1;
        }
        std::ranges::sort(apps);
        apps.erase(std::ranges::unique(apps).begin(), apps.end());
        return apps;
    }

    inline std::string Join(const std::vector<std::string>& apps) {
        std::string csv;
        for (const auto& app : apps) {
            if (!csv.empty()) {
                csv += ", ";
            }
            csv += app;
        }
        return csv;
    }
}
