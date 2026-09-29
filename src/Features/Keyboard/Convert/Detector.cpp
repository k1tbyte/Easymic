#include "Detector.hpp"

#include <algorithm>
#include <optional>
#include <ranges>
#include <utility>
#include "Platform/Str.hpp"

namespace Convert {

namespace {

    // Under three trigrams one sample decides: "щас" reads as the likelier "ofc"
    constexpr size_t NgramMinLetters = 5;
    // Measured on typos and brands: fewer keys or a thinner margin switch words typed right
    constexpr size_t EarlyKeys = 4;
    constexpr double EarlyMargin = 1.0;

    struct Candidate {
        const Side& Typed;
        const Side& Other;
        const std::wstring& Text;
        const std::wstring& Converted;
        size_t Letters;
        int HitsSource;
        int HitsCandidate;
        bool Signs;
        bool Frequency;
        bool Stretched = false;
        std::optional<bool> Blocked;
    };

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

    bool _isIdentifierOrFlag(const std::wstring_view text) {
        return text.starts_with(L'-') || std::ranges::any_of(text, [](const wchar_t c) { return c >= L'0' && c <= L'9'; });
    }

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
            const std::wstring_view word = TrimWord(std::wstring_view(part.begin(), part.end()));
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
    bool _signsAreLetters(const std::wstring_view text, const std::wstring_view converted) {
        return std::ranges::all_of(converted, _alpha) && _alpha(text.back())
               && std::ranges::count_if(text, _alpha) >= 3 && (!_alpha(text.front()) || _alpha(text[1]));
    }

    /// A fix into text no word of the other side could be was typed right (`гпт` is not `ugn`).
    bool _blocked(Candidate& c) {
        if (!c.Blocked) {
            c.Blocked = c.Signs || (!c.HitsCandidate && !_plausible(*c.Other.Pack, c.Converted, false));
        }
        return *c.Blocked;
    }

    /// A word of both or a lone letter: a sure fix of the next word converts it too (`d ljvt`).
    void _ambiguous(Verdict& v, const Candidate& c) {
        v.Ambiguous = true;
        const bool joinsNext = c.Letters != std::wstring::npos
                               && ((c.HitsSource > 0 && c.Letters >= 2) || c.Text.size() == 1);
        v.Undecided = c.HitsCandidate > 0 && joinsNext;
    }

    bool _kept(Verdict& v, const Candidate& c) {
        if (c.Frequency && c.Letters == 1) {
            _ambiguous(v, c);
            return true;
        }
        if (c.HitsSource <= 0 || c.HitsSource < c.HitsCandidate) {
            return false;
        }
        if (c.HitsSource == c.HitsCandidate) {
            _ambiguous(v, c);
        } else {
            v.ByDictionary = true;
        }
        return true;
    }

    bool _byRule(Verdict& v, Candidate& c, const Rules* rules) {
        const uint8_t script = c.Typed.Table->Script();
        if (!rules || c.Stretched || script == c.Other.Table->Script()) {
            return false;
        }
        const RuleHit hit = rules->Find(c.Text, script == CyrillicScript);
        if (!hit) {
            return false;
        }
        const bool allLetters = std::ranges::all_of(c.Text, _alpha) || _signsAreLetters(c.Text, c.Converted);
        // Short anchored rules lack enough trigrams; longer words must agree with the model.
        const bool needsMargin = hit.Anywhere || c.Letters >= NgramMinLetters;
        const bool agreed = (c.HitsCandidate > 0 || allLetters) && (!needsMargin || v.Margin() > 0);
        if ((c.Frequency && !agreed) || _blocked(c)) {
            return false;
        }
        v.WrongLayout = true;
        v.ByRule = true;
        v.Rule = hit.Pattern;
        return true;
    }

    bool _byDictionary(Verdict& v, const Candidate& c) {
        if (c.HitsCandidate <= c.HitsSource || c.Letters <= 1) {
            return false;
        }
        v.WrongLayout = true;
        v.ByDictionary = true;
        return true;
    }

    void _byNgram(Verdict& v, Candidate& c, const double thresholdOverride) {
        if (c.Letters <= 2) {
            _ambiguous(v, c);
            return;
        }
        if (!c.Frequency || c.Stretched || c.Letters < NgramMinLetters) {
            return;
        }
        // Letters on the other side count: "и.т.д" makes three ("b/n/l")
        if (static_cast<size_t>(std::ranges::count_if(c.Converted, _alpha)) < NgramMinLetters) {
            return;
        }
        const double threshold = thresholdOverride > 0.0 ? thresholdOverride : c.Other.Pack->Threshold();
        v.WrongLayout = v.Margin() > threshold && !_blocked(c);
    }

    void _decide(Verdict& v, Candidate& c, const Rules* rules, const double thresholdOverride) {
        if (_kept(v, c)) {
            return;
        }
        c.Stretched = c.Frequency && _stretched(c.Text);
        if (_byRule(v, c, rules) || _byDictionary(v, c)) {
            return;
        }
        _byNgram(v, c, thresholdOverride);
    }

} // anonymous namespace

Verdict Detect(std::span<const Key> word, const Side& typed, const Side& other,
               const double thresholdOverride, const Rules* rules, const bool frequency) {
    Verdict v;
    const std::wstring text = typed.Table->Render(word);
    if (text.empty()) {
        return v;
    }
    v.Typed = text;
    v.SourceLocale = typed.Pack->Locale();

    const std::wstring converted = other.Table->Render(word);
    if (converted.empty() || converted == text) {
        return v;
    }
    v.Fixed = converted;
    v.FixedLocale = other.Pack->Locale();
    if (_isIdentifierOrFlag(text)) {
        return v;
    }

    const auto [hitsSource, words] = _hits(*typed.Pack, text);
    const size_t letters = words == 1 ? TrimWord(text).size() : std::wstring::npos;
    if (letters == 0) {
        return v;
    }
    v.ScoreOriginal = typed.Pack->Score(text);
    v.ScoreFixed = other.Pack->Score(converted);

    const bool signs = _signsForLetters(text, converted);
    Candidate candidate{.Typed = typed,
                        .Other = other,
                        .Text = text,
                        .Converted = converted,
                        .Letters = letters,
                        .HitsSource = hitsSource,
                        .HitsCandidate = signs ? 0 : _hits(*other.Pack, converted).first,
                        .Signs = signs,
                        .Frequency = frequency};
    _decide(v, candidate, rules, thresholdOverride);
    v.Implausible = candidate.Blocked.value_or(false);
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
        || !((std::ranges::all_of(text, _alpha) && std::ranges::all_of(converted, _alpha))
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
    const bool agreed = v.Margin() > EarlyMargin;
    const bool switches = frequency ? agreed && (hit || word.size() >= EarlyKeys) : static_cast<bool>(hit);
    v.Implausible = switches && !_plausible(*other.Pack, converted, true);
    v.WrongLayout = switches && !v.Implausible;
    v.ByRule = v.WrongLayout && hit;
    v.ByDictionary = v.WrongLayout && !hit;
    v.Rule = hit.Pattern;
    return v;
}

}
