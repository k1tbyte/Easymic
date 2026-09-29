#include "Autocorrect.hpp"

#include <string>
#include <utility>

#include "Core/Dispatcher.hpp"
#include "Platform/Str.hpp"
#include "definitions.h"

namespace Autocorrect {

namespace {

    const char* _raw(const std::string& text) {
        return text.c_str();
    }

    template <typename T>
    T _raw(const T value) {
        return value;
    }

    /// Arguments are copied into the posted line: strings by value, `const char*` only literals.
    template <typename... Args>
    void _info(const Runtime& runtime, const char* format, Args... args) {
        if (!runtime.LogDecisions) {
            return;
        }
        Dispatcher::Post([format, ...args = std::move(args)] {
            Logger::Log(Logger::Level::Info, format, _raw(args)...);
        });
    }
}

    void Log(const Runtime& runtime, const Convert::Verdict& verdict) {
        if (!runtime.LogDecisions || verdict.SourceLocale.empty()) {
            return;
        }
        _info(runtime, "Keyboard: %s%s %s -> %s (%s%s, margin=%.2f)", verdict.WrongLayout ? "FIX" : "ok",
              verdict.Early ? " early" : "", Str::WideToUtf8(verdict.Typed), Str::WideToUtf8(verdict.Fixed),
              verdict.Reason(), verdict.ByRule ? " " + Str::WideToUtf8(verdict.Rule) : std::string(), verdict.Margin());
    }

    void LogSkip(const Runtime& runtime, const char* why, const uint64_t detail) {
        _info(runtime, "Keyboard: Space skipped, %s (%llx)", why, detail);
    }

    void LogFix(const Runtime& runtime, const Convert::Verdict& verdict, const bool landed, const uint64_t spaceAt) {
        if (verdict.WrongLayout) {
            _info(runtime, "Keyboard: fix %s after %llu ms", landed ? "landed" : "dropped", GetTickCount64() - spaceAt);
        }
    }

    void LogRun(const Runtime& runtime, const TypedWord::Word& run) {
        const Convert::LayoutTable* table = runtime.LogDecisions ? runtime.Find(run.Layout) : nullptr;
        if (table) {
            _info(runtime, "Keyboard: FIX %s too, the next word decided",
                  Str::WideToUtf8(table->Render({run.Keys.data(), run.Count})));
        }
    }
}
