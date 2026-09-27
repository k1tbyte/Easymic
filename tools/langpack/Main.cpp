#include "Console.hpp"
#include "PackBuilder.hpp"

#include "Features/Keyboard/Convert/Alphabet.hpp"
#include "Features/Keyboard/Convert/Detector.hpp"
#include "Features/Keyboard/Convert/LayoutTable.hpp"
#include "Features/Keyboard/Convert/Pack.hpp"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

void _line(const std::wstring& s) { Console::Print(s + L"\n"); }

std::wstring _num(double d, int precision = 2) {
    std::wostringstream out;
    out << std::fixed << std::setprecision(precision) << d;
    return out.str();
}

std::wstring _wide(const std::string& s) { return Console::FromUtf8(s); }

std::vector<std::string> _split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::istringstream in(s);
    std::string part;
    while (std::getline(in, part, sep)) {
        if (!part.empty()) {
            out.push_back(part);
        }
    }
    return out;
}

std::string _languageOf(HKL hkl) {
    const LANGID langId = LOWORD(reinterpret_cast<uintptr_t>(hkl));
    wchar_t name[16]{};
    if (!GetLocaleInfoW(MAKELCID(langId, SORT_DEFAULT), LOCALE_SISO639LANGNAME, name, 16)) {
        return {};
    }
    std::string out;
    for (const wchar_t* p = name; *p; ++p) {
        out.push_back(static_cast<char>(*p));
    }
    return out;
}

struct InstalledLayout {
    HKL Handle;
    std::string Id;
    std::string Language;
    std::unique_ptr<Convert::LayoutTable> Table;
};

std::vector<InstalledLayout> _loadInstalled() {
    const int count = GetKeyboardLayoutList(0, nullptr);
    if (count <= 0) {
        return {};
    }
    std::vector<HKL> handles(static_cast<size_t>(count));
    GetKeyboardLayoutList(count, handles.data());
    std::vector<InstalledLayout> out;
    for (HKL hkl : handles) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%08x",
                      static_cast<unsigned>(reinterpret_cast<uintptr_t>(hkl) & 0xFFFFFFFF));
        InstalledLayout il;
        il.Handle = hkl;
        il.Id = buf;
        il.Language = _languageOf(hkl);
        il.Table = std::make_unique<Convert::LayoutTable>(hkl);
        out.push_back(std::move(il));
    }
    return out;
}

struct ReverseMap {
    std::unordered_map<wchar_t, Convert::Key> Map;
};

ReverseMap _buildReverse(const Convert::LayoutTable& table) {
    ReverseMap rm;
    for (uint8_t vk = 0x20; vk != 0; ++vk) {
        for (const bool shift : {false, true}) {
            const Convert::Key key{vk, shift, false};
            const wchar_t c = table.Char(key);
            if (c && rm.Map.find(c) == rm.Map.end()) {
                rm.Map[c] = key;
            }
        }
    }
    return rm;
}

int _runLayouts(const std::vector<std::string>&) {
    const auto layouts = _loadInstalled();
    for (const auto& il : layouts) {
        size_t keyCount = 0;
        for (uint8_t vk = 0x20; vk != 0; ++vk) {
            for (const bool shift : {false, true}) {
                if (il.Table->Char({vk, shift, false})) {
                    ++keyCount;
                }
            }
        }
        _line(L"  " + _wide(il.Id) + L"  lang=" + _wide(il.Language) +
              L"  keys=" + std::to_wstring(keyCount));
    }
    return layouts.empty() ? 1 : 0;
}

int _runConvert(const std::vector<std::string>& args) {
    if (args.size() < 3) {
        _line(L"usage: langpack convert <from-lang> <to-lang>");
        return 2;
    }
    const auto layouts = _loadInstalled();
    const InstalledLayout* from = nullptr;
    const InstalledLayout* to = nullptr;
    for (const auto& il : layouts) {
        if (!from && il.Language == args[1]) {
            from = &il;
        }
        if (!to && il.Language == args[2]) {
            to = &il;
        }
    }
    if (!from || !to) {
        _line(L"no layout for one of the languages");
        return 1;
    }
    const ReverseMap rev = _buildReverse(*from->Table);
    std::wstring input;
    while (Console::ReadLine(input)) {
        std::wstring out;
        out.reserve(input.size());
        for (wchar_t c : input) {
            const auto it = rev.Map.find(c);
            if (it == rev.Map.end()) {
                out.push_back(c);
            } else {
                const wchar_t mapped = to->Table->Char(it->second);
                out.push_back(mapped ? mapped : c);
            }
        }
        _line(out);
    }
    return 0;
}

int _runPack(const std::vector<std::string>& args) {
    if (args.size() < 4) {
        _line(L"usage: langpack pack <wordlist.txt> <locale> <out.pack> [--threshold X] [--fp X]");
        return 2;
    }
    PackOptions options;
    options.Locale = args[2];
    for (size_t i = 4; i + 1 < args.size(); i += 2) {
        if (args[i] == "--threshold") {
            options.Threshold = std::stod(args[i + 1]);
        } else if (args[i] == "--fp") {
            options.FalsePositiveRate = std::stod(args[i + 1]);
        }
    }
    std::string error;
    if (!BuildPack(args[1], options, args[3], &error)) {
        _line(_wide(error));
        return 1;
    }
    _line(_wide(args[3]) + L"  " +
          _num(std::filesystem::file_size(args[3]) / 1048576.0) + L" MB");
    return 0;
}

int _runLookup(const std::vector<std::string>& args) {
    if (args.size() < 3) {
        _line(L"usage: langpack lookup <pack> <word> [word ...]");
        return 2;
    }
    Convert::Pack pack;
    std::wstring error;
    if (!pack.Load(_wide(args[1]), &error)) {
        _line(error);
        return 1;
    }
    for (size_t i = 2; i < args.size(); ++i) {
        const std::wstring word = _wide(args[i]);
        _line((pack.Contains(word) ? L"hit  " : L"miss ") + word + L"   score " +
              _num(pack.Score(word)));
    }
    return 0;
}

// Punto's data as UTF-8 rule lines: ps.dat is XOR 0xAA but CR and LF, triggers.dat plain,
// both CP1251; a trigger is a word begin
int _runRules(const std::vector<std::string>& args) {
    if (args.size() < 3) {
        _line(L"usage: langpack rules <out.rules> <ps.dat> [triggers.dat]");
        return 2;
    }
    std::string out;
    for (size_t i = 2; i < args.size(); ++i) {
        std::ifstream in(args[i], std::ios::binary);
        if (!in) {
            _line(L"cannot read " + _wide(args[i]));
            return 1;
        }
        std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const bool triggers = i == 3;
        for (char& c : bytes) {
            c = triggers || c == '\r' || c == '\n' ? c : static_cast<char>(c ^ 0xAA);
        }
        const int n = MultiByteToWideChar(1251, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
        std::wstring text(static_cast<size_t>(n), L'\0');
        MultiByteToWideChar(1251, 0, bytes.data(), static_cast<int>(bytes.size()), text.data(), n);
        std::wistringstream lines(text);
        std::wstring line;
        for (bool header = triggers; std::getline(lines, line); header = false) {
            if (!line.empty() && line.back() == L'\r') {
                line.pop_back();
            }
            if (!header && !line.empty()) {
                out += Console::ToUtf8((triggers ? L"_B " : L"") + line) + "\n";
            }
        }
    }
    std::ofstream(args[1], std::ios::binary) << out;
    Convert::Rules rules;
    if (!rules.Load(args[1])) {
        _line(L"cannot write " + _wide(args[1]));
        return 1;
    }
    _line(_wide(args[1]) + L"  " + std::to_wstring(rules.Count()) + L" rules");
    return 0;
}

int _runRepl(const std::vector<std::string>& args) {
    std::string packDir = "packs";
    std::string pair;
    double thresholdOverride = 0.0;
    Convert::Rules rules;
    for (size_t i = 0; i + 1 < args.size(); i += 2) {
        if (args[i] == "--rules") {
            if (!rules.Load(args[i + 1])) {
                _line(L"cannot read " + _wide(args[i + 1]));
                return 1;
            }
        } else if (args[i] == "--packs") {
            packDir = args[i + 1];
        } else if (args[i] == "--pair") {
            pair = args[i + 1];
        } else if (args[i] == "--threshold") {
            thresholdOverride = std::stod(args[i + 1]);
        }
    }

    const auto langs = _split(pair, ',');
    if (langs.size() != 2) {
        _line(L"usage: langpack [--packs dir] [--pair en,ru] [--threshold X]");
        return 2;
    }

    const auto layouts = _loadInstalled();

    struct BoundSide {
        Convert::Pack Pack;
        const InstalledLayout* Layout = nullptr;
    };
    BoundSide sides[2];
    for (int s = 0; s < 2; ++s) {
        std::wstring packPath;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(packDir, ec)) {
            if (entry.path().extension() != ".pack") {
                continue;
            }
            Convert::Pack probe;
            if (probe.Load(entry.path().wstring()) &&
                std::string(probe.Locale()) == langs[s]) {
                packPath = entry.path().wstring();
                break;
            }
        }
        if (packPath.empty() || !sides[s].Pack.Load(packPath)) {
            _line(L"no pack for " + _wide(langs[s]));
            return 1;
        }
        for (const auto& il : layouts) {
            if (il.Language == langs[s]) {
                sides[s].Layout = &il;
                break;
            }
        }
        if (!sides[s].Layout) {
            _line(L"no layout for " + _wide(langs[s]));
            return 1;
        }
    }

    for (int s = 0; s < 2; ++s) {
        _line(L"pack " + _wide(sides[s].Pack.Locale()) + L"  words=" +
              std::to_wstring(sides[s].Pack.WordCount()) + L"  " +
              _num(sides[s].Pack.MappedBytes() / 1048576.0) + L" MB  layout=" +
              _wide(sides[s].Layout->Id));
    }

    Convert::LanguageContext context;
    std::wstring input;
    while (Console::ReadLine(input)) {
        if (input.empty()) {
            break;
        }

        const Convert::Alphabet alph0(sides[0].Pack.Symbols());
        const Convert::Alphabet alph1(sides[1].Pack.Symbols());
        const double cov0 = alph0.Coverage(input);
        const double cov1 = alph1.Coverage(input);
        const int typedIdx = cov0 >= cov1 ? 0 : 1;
        const int otherIdx = 1 - typedIdx;

        const ReverseMap rev = _buildReverse(*sides[typedIdx].Layout->Table);
        std::vector<Convert::Key> keys;
        for (wchar_t c : input) {
            const auto it = rev.Map.find(c);
            if (it != rev.Map.end()) {
                keys.push_back(it->second);
            } else {
                keys.push_back({0, false, false});
            }
        }

        const Convert::Side typed{sides[typedIdx].Layout->Table.get(), &sides[typedIdx].Pack};
        const Convert::Side other{sides[otherIdx].Layout->Table.get(), &sides[otherIdx].Pack};

        context.Typing(sides[typedIdx].Pack.Locale());
        const Convert::Verdict v = Convert::Detect(keys, typed, other, &context, thresholdOverride,
                                                   rules.Count() ? &rules : nullptr);
        if (v.SourceLocale.empty()) {
            _line(L"  no language matched");
        } else {
            const std::wstring why = _wide(v.Reason()) + (v.ByRule ? L" " + v.Rule : L"") + L" margin=" + _num(v.Margin()) +
                                     L" pref=" + _num(v.Preference);
            if (v.WrongLayout) {
                _line(L"  FIX -> " + v.Fixed + L"   [" + _wide(std::string(v.SourceLocale)) +
                      L" to " + _wide(std::string(v.FixedLocale)) + L", " + why + L"]");
            } else {
                _line(L"  ok   [" + _wide(std::string(v.SourceLocale)) + L", " + why +
                      L", alt: " + v.Fixed + L"]");
            }
        }
        Convert::FeedContext(context, v, input);
        // As the app does once a fix lands
        if (v.WrongLayout) {
            context.Switched(v.FixedLocale);
        }
    }
    return 0;
}

} // anonymous namespace

int main() {
    Console::Init();
    const std::vector<std::string> full = Console::CommandLineUtf8();
    const std::vector<std::string> args(full.begin() + (full.empty() ? 0 : 1), full.end());
    if (!args.empty() && args[0] == "pack") {
        return _runPack(args);
    }
    if (!args.empty() && args[0] == "lookup") {
        return _runLookup(args);
    }
    if (!args.empty() && args[0] == "layouts") {
        return _runLayouts(args);
    }
    if (!args.empty() && args[0] == "convert") {
        return _runConvert(args);
    }
    if (!args.empty() && args[0] == "rules") {
        return _runRules(args);
    }
    return _runRepl(args);
}
