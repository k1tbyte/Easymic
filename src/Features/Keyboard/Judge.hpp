#pragma once

#include <cstdint>
#include <memory>

#include "Convert/Detector.hpp"
#include "TypedWord.hpp"

namespace Autocorrect {
    struct Runtime;
}

/// Judges the word while it is typed, on the threadpool, so its Space finds the verdict ready and a kept word
/// never holds the typing.
namespace Judge {

    /// Once, before the input thread runs.
    void Register();

    /// Input thread, outside OnKey: what the judge decides with; null stops it.
    void Use(std::shared_ptr<const Autocorrect::Runtime> runtime);

    /// Input thread, from OnKey: the word is now `word`, version `gen`. Copies it and wakes the judge, no allocation.
    void Typed(const TypedWord::Word& word, uint32_t gen);

    /// Input thread: the verdict on version `gen` read in `layout`; null while it is not in.
    const Convert::Verdict* Ready(uint32_t gen, HKL layout);

    /// Input thread, from OnKey: the ready verdict was kept; logs it off the thread when asked, taking it.
    void LogKept();
}
