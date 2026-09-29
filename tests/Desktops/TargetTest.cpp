#include <cstdio>

#include "../Check.hpp"
#include "Features/Desktops/Target.hpp"

namespace {

    using Test::Check;
    using Desktops::ParseTarget;

    void _steps() {
        const auto next = ParseTarget("next");
        Check(next && next->Step == 1 && next->Index == -1 && next->Name.empty(), "next steps forward");
        const auto prev = ParseTarget("prev");
        Check(prev && prev->Step == -1, "prev steps back");
    }

    void _numbers() {
        const auto first = ParseTarget("1");
        Check(first && first->Index == 0 && first->Step == 0 && first->Name.empty(), "numbers count from 1");
        const auto twelfth = ParseTarget("12");
        Check(twelfth && twelfth->Index == 11, "two digits");
        Check(!ParseTarget("0"), "zero is declined");
        Check(!ParseTarget("-2"), "a negative number is declined");
    }

    void _names() {
        const auto work = ParseTarget("Work");
        Check(work && work->Name == "Work" && work->Index == -1 && work->Step == 0, "a word is a name");
        Check(ParseTarget("3x")->Name == "3x", "digits followed by text are a name");
        Check(ParseTarget("Next")->Name == "Next", "the keywords are lowercase only");
        Check(ParseTarget("99999999999")->Name == "99999999999", "a number that overflows is a name");
        Check(!ParseTarget(""), "empty is declined");
    }
}

int main() {
    _steps();
    _numbers();
    _names();

    std::printf(Test::Failures ? "%d check(s) failed\n" : "all desktop target checks passed\n", Test::Failures);
    return Test::Failures ? 1 : 0;
}
