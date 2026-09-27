#include "Detector.hpp"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <utility>

namespace Convert {

namespace {

    constexpr double kShortContextMin = 0.25;
    constexpr double kTieContextMin = 0.5;
    constexpr double kContextWeight = 1.0;
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

} // anonymous namespace

void LanguageContext::Note(std::string_view locale) {
    if (locale.empty()) {
        return;
    }
    _ring[_head] = std::string(locale);
    _head = (_head + 1) % Depth;
    if (_count < Depth) {
        ++_count;
    }
}

double LanguageContext::Preference(std::string_view source, std::string_view candidate) const {
    if (_count == 0) {
        return 0.0;
    }
    double forSource = 0.0, forCandidate = 0.0;
    for (size_t i = 0; i < _count; ++i) {
        const std::string& loc = _ring[i];
        if (loc == source) {
            forSource += 1.0;
        } else if (loc == candidate) {
            forCandidate += 1.0;
        }
    }
    return (forCandidate - forSource) / static_cast<double>(_count);
}

Verdict Detect(std::span<const Key> word, const Side& typed, const Side& other,
               const LanguageContext* context, double thresholdOverride, const Rules* rules) {
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

    const double scoreCandidate = other.Pack->Score(converted);
    v.ScoreFixed = scoreCandidate;

    const double preference =
        context ? context->Preference(v.SourceLocale, v.FixedLocale) : 0.0;

    v.Preference = preference;

    if (rules && typed.Table->Script() != other.Table->Script()) {
        // A pattern never flips a known word; punctuation or a mid-word pattern needs the
        // dictionary's or the ngram's agreement too (compounds, "ofc.", "uk,ru")
        const RuleHit hit = rules->Find(text, typed.Table->Script() == kCyrillic);
        const bool known = hitsSource > 0 && hitsCandidate == 0;
        const bool plain = std::ranges::all_of(text, [](const wchar_t c) { return IsCharAlphaW(c); });
        if (hit && !known && (hitsCandidate > 0 || plain) && (!hit.Anywhere || v.Margin() > 0)) {
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

    const auto decideByContext = [&](double minimum) {
        v.ByContext = true;
        v.WrongLayout = preference >= minimum;
        v.Undecided = !v.WrongLayout && preference >= 0 && hitsSource && hitsCandidate
                      && letters >= 2 && letters != std::wstring::npos;
    };
    if (letters <= 2) {
        decideByContext(kShortContextMin);
        return v;
    }

    if (hitsSource > hitsCandidate) {
        v.ByDictionary = true;
        return v;
    }

    if (hitsSource > 0) {
        decideByContext(kTieContextMin);
        return v;
    }

    if (letters < kNgramMinLetters) {
        return v;
    }

    const double threshold =
        thresholdOverride > 0.0 ? thresholdOverride : other.Pack->Threshold();
    v.WrongLayout = v.Margin() > threshold - kContextWeight * preference;
    return v;
}

void FeedContext(LanguageContext& ctx, const Verdict& v, std::wstring_view typedText) {
    // A fix counts once it lands (`Switched`): a declined one must not tilt the context
    const bool confident = v.ByContext && std::abs(v.Preference) >= 0.6;
    if (!v.WrongLayout && ((v.ByDictionary && TrimWord(typedText).size() >= 3) || confident)) {
        ctx.Note(v.SourceLocale);
    }
}

}
