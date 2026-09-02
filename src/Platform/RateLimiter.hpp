//
// Created by kitbyte on 20.11.2025.
//

#pragma once

#ifdef _DEBUG
// Debug only: this header reaches every window through BaseWindow, and <iostream> would plant
// an ios_base::Init static in every translation unit of a release build.
#include <chrono>
#include <stdexcept>

class RateLimiter {
public:
    RateLimiter(int maxCalls, std::chrono::milliseconds interval)
        : maxCalls_(maxCalls),
          interval_(interval),
          tokens_(maxCalls),
          lastRefill_(std::chrono::steady_clock::now()) {}

    bool TooManyCalls() {
        using namespace std::chrono;
        const auto now = steady_clock::now();

        const auto elapsed = duration_cast<milliseconds>(now - lastRefill_);
        if (elapsed >= interval_) {
            tokens_ = maxCalls_;
            lastRefill_ = now;
        }

        if (tokens_ > 0) {
            --tokens_;
            return false;
        }
        return true;
    }

private:
    int maxCalls_;
    std::chrono::milliseconds interval_;
    int tokens_;
    std::chrono::steady_clock::time_point lastRefill_;
};

#define MEASURE_RATE(name, maxCalls, intervalMs, alert) \
    static RateLimiter name(maxCalls, std::chrono::milliseconds(intervalMs)); \
    if (name.TooManyCalls()) { \
        alert; \
    }
#else
#define MEASURE_RATE(name, maxCalls, intervalMs, alert)
#endif

