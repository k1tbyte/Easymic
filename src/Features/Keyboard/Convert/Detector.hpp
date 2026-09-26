#pragma once

#include "LayoutTable.hpp"
#include "Pack.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Convert {

    class LanguageContext {
    public:
        void Note(std::string_view locale);
        void Clear() { _count = 0; }
        bool Empty() const { return _count == 0; }
        double Preference(std::string_view source, std::string_view candidate) const;

    private:
        static constexpr size_t Depth = 6;
        std::array<std::string, Depth> _ring;
        size_t _head = 0;
        size_t _count = 0;
    };

    struct Side {
        const LayoutTable* Table;
        const Pack* Pack;
    };

    struct Verdict {
        bool WrongLayout = false;
        std::wstring Typed;
        std::wstring Fixed;
        double ScoreOriginal = 0.0;
        double ScoreFixed = 0.0;
        bool ByDictionary = false;
        bool ByContext = false;
        double Preference = 0.0;
        std::string_view SourceLocale;
        std::string_view FixedLocale;

        double Margin() const { return ScoreFixed - ScoreOriginal; }
        const char* Reason() const {
            return ByDictionary ? "dict" : ByContext ? "context" : "ngram";
        }
    };

    Verdict Detect(std::span<const Key> word, const Side& typed, const Side& other,
                   const LanguageContext* context = nullptr, double thresholdOverride = 0);

    void FeedContext(LanguageContext& ctx, const Verdict& v, std::wstring_view typedText);

}
