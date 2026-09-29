#pragma once

#include <cstdio>

namespace Test {

    inline int Failures = 0;

    inline void Check(const bool ok, const char* what) {
        if (ok) {
            return;
        }
        std::printf("FAIL: %s\n", what);
        ++Failures;
    }
}
