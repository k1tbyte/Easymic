#pragma once

#include "LayoutTable.hpp"
#include "Pack.hpp"
#include "Rules.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Convert {

    class LanguageContext {
    public:
        void Note(std::string_view locale);
        /// `Preference` reads from slot 0, so writing restarts there too
        void Clear() { _count = 0; _head = 0; }
        bool Empty() const { return _count == 0; }
        double Preference(std::string_view source, std::string_view candidate) const;
        /// The next word is typed in a layout of `locale`. Another one than the last word's is a
        /// switch: the words before it stop counting.
        void Typing(std::string_view locale) {
            if (locale != _typing) {
                _typing = locale;
                Clear();
            }
        }
        /// A fix or a conversion switched the layout to `locale`: the context restarts with it.
        void Switched(std::string_view locale) {
            Typing(locale);
            Note(locale);
        }

    private:
        static constexpr size_t Depth = 6;
        std::array<std::string, Depth> _ring;
        size_t _head = 0;
        size_t _count = 0;
        std::string _typing;
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
        bool ByRule = false;
        /// The user taught this word: over every other signal.
        bool ByUser = false;
        /// A word in both languages the context did not settle: the next word decides.
        bool Undecided = false;
        std::wstring Rule;
        double Preference = 0.0;
        std::string_view SourceLocale;
        std::string_view FixedLocale;

        double Margin() const { return ScoreFixed - ScoreOriginal; }
        const char* Reason() const {
            return ByUser ? "user" : ByRule ? "rule" : ByDictionary ? "dict" : ByContext ? "context" : "ngram";
        }
    };

    Verdict Detect(std::span<const Key> word, const Side& typed, const Side& other,
                   const LanguageContext* context = nullptr, double thresholdOverride = 0,
                   const Rules* rules = nullptr);

    void FeedContext(LanguageContext& ctx, const Verdict& v, std::wstring_view typedText);

}
