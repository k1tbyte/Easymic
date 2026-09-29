#include "Rules.hpp"

#include "LayoutTable.hpp"
#include "Platform/File.hpp"
#include "Platform/Str.hpp"

#include <algorithm>

namespace Convert {

namespace {

    enum : uint8_t { Whole = 0, Begin = 1, Anywhere = 2, Exception = 4, CaseSensitive = 8, Cyrillic = 16 };

    constexpr uint64_t Prime = 1099511628211ull;

    uint64_t _seed(const uint8_t kind) {
        return (14695981039346656037ull ^ kind) * Prime;
    }

    uint64_t _step(uint64_t h, const wchar_t c) {
        h = (h ^ static_cast<uint8_t>(c)) * Prime;
        return (h ^ static_cast<uint8_t>(c >> 8)) * Prime;
    }

    uint64_t _hash(const uint8_t kind, const std::wstring_view text) {
        uint64_t h = _seed(kind);
        for (const wchar_t c : text) {
            h = _step(h, c);
        }
        return h;
    }

} // anonymous namespace

bool Rules::Load(const std::filesystem::path& file) {
    auto bytes = File::Read(file.c_str());
    if (!bytes) {
        return false;
    }
    if (bytes->starts_with("\xEF\xBB\xBF")) {
        bytes->erase(0, 3);
    }
    const std::wstring text = Str::Utf8ToWide(*bytes);
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
    kind |= std::ranges::any_of(line, [](const wchar_t c) { return ScriptOf(c) == CyrillicScript; }) ? Cyrillic : 0;
    _keys.push_back(_hash(kind, kind & CaseSensitive ? std::wstring(line) : Str::Lower(line)));
    _longest = std::max(_longest, line.size());
}

bool Rules::_has(const uint8_t kind, const std::wstring_view text) const {
    return std::ranges::binary_search(_keys, _hash(kind, text));
}

bool Rules::_match(const uint8_t kind, const std::wstring_view word, RuleHit& hit, const bool open) const {
    if ((!open || kind & Exception) && _has(kind | Whole, word)) {
        hit.Pattern = L"P " + std::wstring(word);
        return true;
    }
    // The word between spaces, so an edge space anchors a pattern; hashed as it grows, never copied
    const size_t size = word.size() + (open ? 1 : 2);
    const auto at = [word](const size_t i) { return i == 0 || i > word.size() ? L' ' : word[i - 1]; };
    const auto found = [&](const uint8_t type, const size_t from, const wchar_t* tag) {
        uint64_t h = _seed(kind | type);
        for (size_t n = 1; n <= _longest && from + n <= size; ++n) {
            h = _step(h, at(from + n - 1));
            if (std::ranges::binary_search(_keys, h)) {
                hit.Pattern = tag;
                for (size_t i = from; i < from + n; ++i) {
                    hit.Pattern += at(i);
                }
                return true;
            }
        }
        return false;
    };
    if (found(Begin, 1, L"B ")) {
        return true;
    }
    for (size_t i = 0; i < size; ++i) {
        if (found(Anywhere, i, L"A ")) {
            hit.Anywhere = true;
            return true;
        }
    }
    return false;
}

RuleHit Rules::Find(const std::wstring_view word, const bool cyrillic, const bool open) const {
    if (_keys.empty() || word.empty()) {
        return {};
    }
    const std::wstring lower = Str::Lower(word);
    const uint8_t script = cyrillic ? Cyrillic : 0;
    const auto match = [&](const uint8_t kind, RuleHit& hit) {
        return _match(script | kind, lower, hit, open) || _match(script | kind | CaseSensitive, word, hit, open);
    };
    RuleHit hit, exception;
    return match(0, hit) && !match(Exception, exception) ? hit : RuleHit{};
}

}
