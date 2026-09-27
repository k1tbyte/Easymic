#include "Rules.hpp"

#include "Platform/Str.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>

namespace Convert {

namespace {

    enum : uint8_t { Whole = 0, Begin = 1, Anywhere = 2, Exception = 4, CaseSensitive = 8, Cyrillic = 16 };

    uint64_t _hash(const uint8_t kind, const std::wstring_view text) {
        constexpr uint64_t prime = 1099511628211ull;
        uint64_t h = (14695981039346656037ull ^ kind) * prime;
        for (const wchar_t c : text) {
            h = (h ^ static_cast<uint8_t>(c)) * prime;
            h = (h ^ static_cast<uint8_t>(c >> 8)) * prime;
        }
        return h;
    }

    std::wstring _lower(const std::wstring_view text) {
        std::wstring out(text);
        if (!out.empty()) {
            CharLowerBuffW(out.data(), static_cast<DWORD>(out.size()));
        }
        return out;
    }

} // anonymous namespace

bool Rules::Load(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return false;
    }
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.starts_with("\xEF\xBB\xBF")) {
        bytes.erase(0, 3);
    }
    const std::wstring text = Str::Utf8ToWide(bytes);
    for (size_t start = 0; start < text.size();) {
        const size_t end = std::min(text.find_first_of(L"\r\n", start), text.size());
        _add(std::wstring_view(text).substr(start, end - start));
        start = end + 1;
    }
    std::ranges::sort(_keys);
    _keys.erase(std::ranges::unique(_keys).begin(), _keys.end());
    return true;
}

void Rules::_add(std::wstring_view line) {
    std::wstring_view flags;
    if (line.starts_with(L'_')) {
        const size_t space = line.find(L' ');
        if (space == std::wstring_view::npos) {
            return;
        }
        flags = line.substr(1, space - 1);
        line.remove_prefix(space + 1);
    }
    if (line.find_first_not_of(L' ') == std::wstring_view::npos || flags.contains(L'D')
        || line.starts_with(L"PSVersion=")) {
        return;
    }
    uint8_t kind = flags.contains(L'P') ? Whole : flags.contains(L'B') ? Begin : Anywhere;
    kind |= flags.contains(L'E') ? Exception : 0;
    kind |= flags.contains(L'C') ? CaseSensitive : 0;
    kind |= std::ranges::any_of(line, [](const wchar_t c) { return c >= 0x400 && c <= 0x4FF; }) ? Cyrillic : 0;
    _keys.push_back(_hash(kind, kind & CaseSensitive ? std::wstring(line) : _lower(line)));
    _longest = std::max(_longest, line.size());
}

bool Rules::_has(const uint8_t kind, const std::wstring_view text) const {
    return std::ranges::binary_search(_keys, _hash(kind, text));
}

bool Rules::_match(const uint8_t kind, const std::wstring_view word, RuleHit& hit, const bool open) const {
    const std::wstring edged = L" " + std::wstring(word) + (open ? L"" : L" ");
    const std::wstring_view view = edged;
    if ((!open || kind & Exception) && _has(kind | Whole, word)) {
        hit.Pattern = L"P " + std::wstring(word);
        return true;
    }
    for (size_t n = 1; n <= std::min(_longest, view.size() - 1); ++n) {
        if (_has(kind | Begin, view.substr(1, n))) {
            hit.Pattern = L"B " + edged.substr(1, n);
            return true;
        }
    }
    for (size_t i = 0; i < view.size(); ++i) {
        for (size_t n = 1; n <= _longest && i + n <= view.size(); ++n) {
            if (_has(kind | Anywhere, view.substr(i, n))) {
                hit.Pattern = L"A " + edged.substr(i, n);
                hit.Anywhere = true;
                return true;
            }
        }
    }
    return false;
}

RuleHit Rules::Find(const std::wstring_view word, const bool cyrillic, const bool open) const {
    if (_keys.empty() || word.empty()) {
        return {};
    }
    const std::wstring lower = _lower(word);
    const uint8_t script = cyrillic ? Cyrillic : 0;
    const auto match = [&](const uint8_t kind, RuleHit& hit) {
        return _match(script | kind, lower, hit, open) || _match(script | kind | CaseSensitive, word, hit, open);
    };
    RuleHit hit, exception;
    return match(0, hit) && !match(Exception, exception) ? hit : RuleHit{};
}

}
