#include "Detector.hpp"

#include <algorithm>
#include <ranges>
#include <utility>
#include "Platform/Str.hpp"

namespace Convert {

namespace {

    // Under three trigrams one sample decides: "щас" reads as the likelier "ofc"
    constexpr size_t kNgramMinLetters = 5;
    // Measured on typos and brands: fewer keys or a thinner margin switch words typed right
    constexpr size_t kEarlyKeys = 4;
    constexpr double kEarlyMargin = 1.0;

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

    /// A sign where a letter was typed (`[fnf` from `хата`): no word of the other side.
    /// Only a closing `.` or `,` after three keys is punctuation (`руддщб` = `hello,`); `'` is in words.
    bool _signsForLetters(const std::wstring_view typed, const std::wstring_view converted) {
        size_t wordAt = 0;
        for (size_t i = 0; i < converted.size(); ++i) {
            const wchar_t c = converted[i];
            if (c == L' ') {
                wordAt = i + 1;
                continue;
            }
            const bool closing = (c == L'.' || c == L',') && i - wordAt >= 3
                                 && (i + 1 == converted.size() || converted[i + 1] == L' ');
            if (!_alpha(c) && _alpha(typed[i]) && c != L'\'' && !closing) {
                return true;
            }
        }
        return false;
    }

    /// Text a word of `pack`'s language could be: no trigram of it is missing from the pack, and a short one
    /// (or a start, `open`) begins a known word. Whole words of the pack skip this, they are known.
    bool _plausible(const Pack& pack, const std::wstring_view text, const bool open) {
        for (const auto part : std::views::split(text, L' ')) {
            const std::wstring word = TrimWord(std::wstring_view(part.begin(), part.end()));
            const bool startCounts = open || word.size() <= StartLetters;
            if (!word.empty() && ((startCounts && !pack.Begins(word)) || pack.Unseen(word, open))) {
                return false;
            }
        }
        return true;
    }

    /// A letter typed thrice is an elongation (`дааа`, `Ыыы`), a word of neither side.
    bool _stretched(const std::wstring_view text) {
        const std::wstring lower = Str::Lower(text);
        for (size_t i = 2; i < lower.size(); ++i) {
            if (lower[i] == lower[i - 1] && lower[i] == lower[i - 2] && _alpha(lower[i])) {
                return true;
            }
        }
        return false;
    }

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

    v.ScoreFixed = other.Pack->Score(converted);
    // Dictionary trimming must not turn identifiers or flags into words.
    if (text.starts_with(L'-') || std::ranges::any_of(text, [](const wchar_t c) { return c >= L'0' && c <= L'9'; })) {
        return v;
    }

    const auto [hitsSource, words] = _hits(*typed.Pack, text);
    const size_t letters = words == 1 ? TrimWord(text).size() : std::wstring::npos;
    if (letters == 0) {
        return v;
    }
    const bool signs = _signsForLetters(text, converted);
    const int hitsCandidate = signs ? 0 : _hits(*other.Pack, converted).first;
    // A fix into text no word of the other side could be was typed right (`гпт` is not `ugn`)
    const auto blocked = [&] {
        return v.Implausible = signs || (!hitsCandidate && !_plausible(*other.Pack, converted, false));
    };
    // A word of both or a lone letter: a sure fix of the next word converts it too (`d ljvt`)
    const auto ambiguous = [&] {
        v.Ambiguous = true;
        v.Undecided = hitsCandidate && letters != std::wstring::npos && (hitsSource && letters >= 2 || text.size() == 1);
    };
    if (frequency && letters == 1) {
        ambiguous();
        return v;
    }
    if (hitsSource > 0 && hitsSource >= hitsCandidate) {
        if (hitsSource == hitsCandidate) {
            ambiguous();
        } else {
            v.ByDictionary = true;
        }
        return v;
    }

    // Only a dictionary switches an elongation
    const bool stretched = frequency && _stretched(text);
    if (rules && !stretched && typed.Table->Script() != other.Table->Script()) {
        const RuleHit hit = rules->Find(text, typed.Table->Script() == CyrillicScript);
        const bool allLetters = std::ranges::all_of(text, _alpha) || _signsAreLetters(text, converted);
        // Short anchored rules lack enough trigrams; longer words must agree with the model.
        const bool agreed = (hitsCandidate > 0 || allLetters)
                            && (!(hit.Anywhere || letters >= kNgramMinLetters) || v.Margin() > 0);
        if (hit && (agreed || !frequency) && !blocked()) {
            v.WrongLayout = v.ByRule = true;
            v.Rule = hit.Pattern;
            return v;
        }
    }

    if (hitsCandidate > hitsSource && letters > 1) {
        v.WrongLayout = true;
        v.ByDictionary = true;
        return v;
    }

    if (letters <= 2) {
        ambiguous();
        return v;
    }

    // Letters on the other side count: "и.т.д" makes three ("b/n/l")
    if (!frequency || stretched || letters < kNgramMinLetters
        || std::ranges::count_if(converted, _alpha) < kNgramMinLetters) {
        return v;
    }

    const double threshold =
        thresholdOverride > 0.0 ? thresholdOverride : other.Pack->Threshold();
    v.WrongLayout = v.Margin() > threshold && !blocked();
    return v;
}

Verdict Early(const std::span<const Key> word, const Side& typed, const Side& other, const Rules* rules,
              const bool frequency) {
    Verdict v;
    if (word.size() < 2 || typed.Table->Script() == other.Table->Script()) {
        return v;
    }
    const std::wstring text = typed.Table->Render(word);
    const std::wstring converted = other.Table->Render(word);
    // A start ending in a sign waits a key: "ok," or "об"
    if (text.empty() || converted.empty() || typed.Pack->Begins(text)
        || !(std::ranges::all_of(text, _alpha) && std::ranges::all_of(converted, _alpha)
             || _signsAreLetters(text, converted))) {
        return v;
    }
    // Its last letter once more would make an elongation (`ыы` of `ыыы`): wait a key
    if (frequency && _stretched(text + text.back())) {
        return v;
    }
    v.Typed = text;
    v.Fixed = converted;
    v.Early = true;
    v.SourceLocale = typed.Pack->Locale();
    v.FixedLocale = other.Pack->Locale();
    v.ScoreOriginal = typed.Pack->Score(text, true);
    v.ScoreFixed = other.Pack->Score(converted, true);
    const RuleHit hit = rules ? rules->Find(text, typed.Table->Script() == CyrillicScript, true) : RuleHit{};
    // Frequency analysis guards a rule as at the word end; alone it waits for more keys
    const bool agreed = v.Margin() > kEarlyMargin;
    v.WrongLayout = frequency ? agreed && (hit || word.size() >= kEarlyKeys) : static_cast<bool>(hit);
    if (v.WrongLayout) {
        v.Implausible = !_plausible(*other.Pack, converted, true);
        v.WrongLayout = !v.Implausible;
    }
    v.ByRule = v.WrongLayout && hit;
    v.ByDictionary = v.WrongLayout && !hit;
    v.Rule = hit.Pattern;
    return v;
}

}
