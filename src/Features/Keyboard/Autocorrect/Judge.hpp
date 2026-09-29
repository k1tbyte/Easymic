#pragma once

#include <cstdint>
#include <memory>

#include "../Convert/Detector.hpp"
#include "TypedWord.hpp"

namespace Autocorrect {
    struct Runtime;
}

/// Judges the word while it is typed, on the threadpool, so its Space finds the verdict ready.
namespace Judge {

    /// Input thread, outside OnKey: the word so far of version `gen`, read in `layout`, switches now.
    using EarlyFix = void (*)(uint32_t gen, HKL layout, const Convert::Verdict& verdict);

    void Register(EarlyFix early);

    /// Input thread, outside OnKey: null stops the judge.
    void Use(std::shared_ptr<const Autocorrect::Runtime> runtime);

    /// Input thread, from OnKey: copies the word and wakes the judge, no allocation.
    void Typed(const TypedWord::Word& word, uint32_t gen);

    /// Null while the verdict on version `gen` read in `layout` is not in.
    const Convert::Verdict* Ready(uint32_t gen, HKL layout);

    /// Input thread, from OnKey: takes the kept verdict to log it off the thread, when asked.
    void LogKept();
}
