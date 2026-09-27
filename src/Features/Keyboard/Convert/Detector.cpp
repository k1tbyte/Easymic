#include "Detector.hpp"

#include <algorithm>
#include <ranges>
#include <utility>

namespace Convert {

namespace {

    // Under three trigrams one sample decides: "щас" reads as the likelier "ofc"
    constexpr size_t kNgramMinLetters = 5;
    constexpr uint8_t kCyrillic = 4;

    /// Dictionary hits among the words of `text` (a joined run holds several), and the word count.
    std::pair<int, size_t> _hits(const Pack& pack, const std::wstring_view text) {
        int hits = 0;
        size_t words = 0;
        for (const auto word : std::views::split(text, L' ')) {
            if (!word.empty()) {
                hits += pack.Contains(std::wstring_view(word.begin(), word.end()));
                ++words;
            }
        }
        return {hits, words};
    }

    bool _alpha(const wchar_t c) { return IsCharAlphaW(c); }

    /// A sign is a letter of the other side (`bv,f` = имба) unless it is punctuation more often: not a letter
    /// there (`ц.у` = `w/e`), at the end (`ofc.`), in a short token (`:p`), after a lone first letter (`e.g`).
    bool _signsAreLetters(const std::wstring& text, const std::wstring& converted) {
        return std::ranges::all_of(converted, _alpha) && _alpha(text.back())
               && std::ranges::count_if(text, _alpha) >= 3 && (!_alpha(text[0]) || _alpha(text[1]));
    }

} // anonymous namespace

Verdict Detect(std::span<const Key> word, const Side& typed, const Side& other,
               double thresholdOverride, const Rules* rules, const bool frequency) {
    Verdict v;
    const std::wstring text = typed.Table->Render(word);
    if (text.empty()) {
        return v;
    }

    v.Typed = text;
    v.SourceLocale = typed.Pack->Locale();
    v.ScoreOriginal = typed.Pack->Score(text);

    const std::wstring converted = other.Table->Render(word);
    if (converted.empty() || converted == text) {
        return v;
    }

    v.Fixed = converted;
    v.FixedLocale = other.Pack->Locale();

    const auto [hitsSource, words] = _hits(*typed.Pack, text);
    const size_t letters = words == 1 ? TrimWord(text).size() : std::wstring::npos;
    // "1." or "...": punctuation, whatever letters the other layout makes of it ("1ю")
    if (letters == 0) {
        return v;
    }
    const int hitsCandidate = _hits(*other.Pack, converted).first;

    v.ScoreFixed = other.Pack->Score(converted);

    if (rules && typed.Table->Script() != other.Table->Script()) {
        // A pattern never flips a known word. Frequency analysis guards it too: punctuation needs the
        // dictionary's agreement, a mid-word pattern the ngram's ("ofc.", "uk,ru")
        const RuleHit hit = rules->Find(text, typed.Table->Script() == kCyrillic);
        const bool known = hitsSource > 0 && hitsCandidate == 0;
        const bool allLetters = std::ranges::all_of(text, _alpha) || _signsAreLetters(text, converted);
        const bool agreed = (hitsCandidate > 0 || allLetters) && (!hit.Anywhere || v.Margin() > 0);
        if (hit && !known && (agreed || !frequency)) {
            v.WrongLayout = v.ByRule = true;
            v.Rule = hit.Pattern;
            return v;
        }
    }

    if (hitsCandidate > hitsSource) {
        v.WrongLayout = true;
        v.ByDictionary = true;
        return v;
    }

    // Too short to tell, or a word of both languages: kept, and the next word decides (the run)
    const auto ambiguous = [&] {
        v.Ambiguous = true;
        v.Undecided = hitsSource && hitsCandidate && letters >= 2 && letters != std::wstring::npos;
    };
    if (letters <= 2) {
        ambiguous();
        return v;
    }

    if (hitsSource > hitsCandidate) {
        v.ByDictionary = true;
        return v;
    }

    if (hitsSource > 0) {
        ambiguous();
        return v;
    }

    // Letters on the other side count: "и.т.д" makes three ("b/n/l")
    if (!frequency || letters < kNgramMinLetters || std::ranges::count_if(converted, _alpha) < kNgramMinLetters) {
        return v;
    }

    const double threshold =
        thresholdOverride > 0.0 ? thresholdOverride : other.Pack->Threshold();
    v.WrongLayout = v.Margin() > threshold;
    return v;
}

}
