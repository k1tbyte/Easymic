#include "Detector.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

namespace Convert {

namespace {

    constexpr double kShortContextMin = 0.25;
    constexpr double kTieContextMin = 0.5;
    constexpr double kContextWeight = 1.0;

    std::vector<std::wstring> _tokenize(const std::wstring& s) {
        std::vector<std::wstring> out;
        std::wistringstream in(s);
        std::wstring tok;
        while (in >> tok) {
            out.push_back(tok);
        }
        return out;
    }

    int _countHits(const Pack& pack, const std::vector<std::wstring>& words) {
        int hits = 0;
        for (const std::wstring& w : words) {
            hits += pack.Contains(w);
        }
        return hits;
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
               const LanguageContext* context, double thresholdOverride) {
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

    const std::vector<std::wstring> srcWords = _tokenize(text);
    const int hitsSource = _countHits(*typed.Pack, srcWords);

    const std::vector<std::wstring> dstWords = _tokenize(converted);
    const int hitsCandidate = _countHits(*other.Pack, dstWords);

    const double scoreCandidate = other.Pack->Score(converted);
    v.ScoreFixed = scoreCandidate;

    const double preference =
        context ? context->Preference(v.SourceLocale, v.FixedLocale) : 0.0;

    v.Preference = preference;

    if (hitsCandidate > hitsSource) {
        v.WrongLayout = true;
        v.ByDictionary = true;
        return v;
    }

    const auto decideByContext = [&](double minimum) {
        v.ByContext = true;
        v.WrongLayout = preference >= minimum;
    };

    const size_t letters =
        srcWords.size() == 1 ? TrimWord(srcWords[0]).size() : std::wstring::npos;
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

    const double threshold =
        thresholdOverride > 0.0 ? thresholdOverride : other.Pack->Threshold();
    v.WrongLayout = v.Margin() > threshold - kContextWeight * preference;
    return v;
}

void FeedContext(LanguageContext& ctx, const Verdict& v, std::wstring_view typedText) {
    const bool confident = v.ByContext && std::abs(v.Preference) >= 0.6;
    if (v.ByDictionary && TrimWord(typedText).size() >= 3) {
        ctx.Note(v.WrongLayout ? v.FixedLocale : v.SourceLocale);
    } else if (confident) {
        ctx.Note(v.WrongLayout ? v.FixedLocale : v.SourceLocale);
    }
}

}
