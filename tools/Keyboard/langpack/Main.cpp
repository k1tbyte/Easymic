#include "Console.hpp"
#include "PackBuilder.hpp"

#include "Features/Keyboard/Convert/Alphabet.hpp"
#include "Features/Keyboard/Convert/Detector.hpp"
#include "Features/Keyboard/Convert/LayoutTable.hpp"
#include "Features/Keyboard/Convert/Pack.hpp"
#include "Features/Keyboard/InputLanguage.hpp"
#include "Platform/File.hpp"
#include "Platform/Str.hpp"

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using Args = std::vector<std::string>;
using ReverseMap = std::unordered_map<wchar_t, Convert::Key>;

constexpr double MegaByte = 1048576.0;
constexpr size_t RulesOutArg = 1;
constexpr size_t PsDatArg = 2;
constexpr size_t TriggersArg = 3;
constexpr UINT Cp1251 = 1251;
constexpr char PsDatXor = static_cast<char>(0xAA);

void _line(const std::wstring& s) { Console::Print(s + L"\n"); }

std::wstring _num(const double d) { return std::format(L"{:.2f}", d); }

std::wstring _wide(const std::string_view s) { return Str::Utf8ToWide(std::string(s)); }

std::filesystem::path _path(const std::string& utf8) { return _wide(utf8); }

std::vector<std::string> _split(const std::string& s, const char separator) {
    std::vector<std::string> parts;
    for (size_t start = 0; start < s.size();) {
        const size_t end = std::min(s.find(separator, start), s.size());
        if (end > start) {
            parts.push_back(s.substr(start, end - start));
        }
        start = end + 1;
    }
    return parts;
}

std::optional<InputLanguage::Layout> _layoutFor(const std::string& language) {
    for (const auto& layout : InputLanguage::Installed()) {
        if (InputLanguage::Iso639(layout.Handle) == language) {
            return layout;
        }
    }
    return std::nullopt;
}

ReverseMap _buildReverse(const Convert::LayoutTable& table) {
    ReverseMap reverse;
    for (uint8_t vk = 0x20; vk != 0; ++vk) {
        for (const bool shift : {false, true}) {
            const Convert::Key key{vk, shift, false};
            if (const wchar_t c = table.Char(key)) {
                reverse.try_emplace(c, key);
            }
        }
    }
    return reverse;
}

int _runConvert(const Args& args) {
    if (args.size() < 3) {
        _line(L"usage: langpack convert <from-lang> <to-lang>");
        return 2;
    }
    const auto from = _layoutFor(args[1]);
    const auto to = _layoutFor(args[2]);
    if (!from || !to) {
        _line(L"no layout for one of the languages");
        return 1;
    }
    const Convert::LayoutTable toTable(to->Handle);
    const ReverseMap reverse = _buildReverse(Convert::LayoutTable(from->Handle));
    std::wstring input;
    while (Console::ReadLine(input)) {
        std::wstring out;
        out.reserve(input.size());
        for (const wchar_t c : input) {
            const auto it = reverse.find(c);
            const wchar_t mapped = it == reverse.end() ? L'\0' : toTable.Char(it->second);
            out.push_back(mapped ? mapped : c);
        }
        _line(out);
    }
    return 0;
}

int _runPack(const Args& args) {
    if (args.size() < 4) {
        _line(L"usage: langpack pack <wordlist.txt> <locale> <out.pack> [--threshold X] [--fp X]");
        return 2;
    }
    PackOptions options{.Locale = args[2]};
    for (size_t i = 4; i + 1 < args.size(); i += 2) {
        if (args[i] == "--threshold") {
            options.Threshold = std::stod(args[i + 1]);
        } else if (args[i] == "--fp") {
            options.FalsePositiveRate = std::stod(args[i + 1]);
        }
    }
    std::string error;
    if (!BuildPack(_path(args[1]), options, _path(args[3]), &error)) {
        _line(_wide(error));
        return 1;
    }
    _line(_wide(args[3]) + L"  " + _num(static_cast<double>(std::filesystem::file_size(_path(args[3]))) / MegaByte) + L" MB");
    return 0;
}

std::optional<std::string> _puntoRules(const std::filesystem::path& file, const bool triggers) {
    auto bytes = File::Read(file.wstring().c_str());
    if (!bytes) {
        return std::nullopt;
    }
    if (!triggers) {
        for (char& c : *bytes) {
            if (c != '\r' && c != '\n') {
                c ^= PsDatXor;
            }
        }
    }
    const int size = MultiByteToWideChar(Cp1251, 0, bytes->data(), static_cast<int>(bytes->size()), nullptr, 0);
    std::wstring text(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(Cp1251, 0, bytes->data(), static_cast<int>(bytes->size()), text.data(), size);

    std::string rules;
    bool header = triggers;
    for (const auto part : std::views::split(std::wstring_view(text), L'\n')) {
        std::wstring_view line(part.begin(), part.end());
        if (!line.empty() && line.back() == L'\r') {
            line.remove_suffix(1);
        }
        if (!header && !line.empty()) {
            rules += Str::WideToUtf8((triggers ? L"_B " : L"") + std::wstring(line)) + "\n";
        }
        header = false;
    }
    return rules;
}

// ps.dat is XOR 0xAA but CR and LF, triggers.dat plain (its first line a header, each a word begin), both CP1251
int _runRules(const Args& args) {
    if (args.size() <= PsDatArg) {
        _line(L"usage: langpack rules <out.rules> <ps.dat> [triggers.dat]");
        return 2;
    }
    std::string rules;
    for (size_t i = PsDatArg; i < args.size(); ++i) {
        const auto decoded = _puntoRules(_path(args[i]), i == TriggersArg);
        if (!decoded) {
            _line(L"cannot read " + _wide(args[i]));
            return 1;
        }
        rules += *decoded;
    }
    const auto out = _path(args[RulesOutArg]);
    Convert::Rules loaded;
    if (!File::Write(out.wstring().c_str(), rules) || !loaded.Load(out)) {
        _line(L"cannot write " + _wide(args[RulesOutArg]));
        return 1;
    }
    _line(_wide(args[RulesOutArg]) + L"  " + std::to_wstring(loaded.Count()) + L" rules");
    return 0;
}

struct ReplOptions {
    std::filesystem::path PackDir = L"packs";
    std::vector<std::string> Languages;
    Convert::Rules Rules;
    double Threshold = 0.0;
    bool Frequency = true;
};

struct BoundSide {
    Convert::Pack Pack;
    std::string LayoutId;
    std::unique_ptr<Convert::LayoutTable> Table;
    ReverseMap Reverse;
};

int _parseRepl(const Args& args, ReplOptions& options) {
    std::string pair;
    for (size_t i = 0; i + 1 < args.size(); i += 2) {
        const std::string& value = args[i + 1];
        if (args[i] == "--frequency") {
            options.Frequency = value != "off";
        } else if (args[i] == "--rules") {
            if (!options.Rules.Load(_path(value))) {
                _line(L"cannot read " + _wide(value));
                return 1;
            }
        } else if (args[i] == "--packs") {
            options.PackDir = _path(value);
        } else if (args[i] == "--pair") {
            pair = value;
        } else if (args[i] == "--threshold") {
            options.Threshold = std::stod(value);
        }
    }
    options.Languages = _split(pair, ',');
    if (options.Languages.size() != 2) {
        _line(L"usage: langpack [--packs dir] [--pair en,ru] [--threshold X] [--frequency off] [--rules file ...]");
        return 2;
    }
    return 0;
}

bool _loadPack(Convert::Pack& pack, const std::filesystem::path& dir, const std::string& language) {
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.path().extension() == L".pack" && pack.Load(entry.path().wstring()) && pack.Locale() == language) {
            return true;
        }
    }
    return false;
}

bool _bind(BoundSide& side, const std::filesystem::path& dir, const std::string& language) {
    if (!_loadPack(side.Pack, dir, language)) {
        _line(L"no pack for " + _wide(language));
        return false;
    }
    const auto layout = _layoutFor(language);
    if (!layout) {
        _line(L"no layout for " + _wide(language));
        return false;
    }
    side.LayoutId = layout->Id;
    side.Table = std::make_unique<Convert::LayoutTable>(layout->Handle);
    side.Reverse = _buildReverse(*side.Table);
    _line(L"pack " + _wide(side.Pack.Locale()) + L"  words=" + std::to_wstring(side.Pack.WordCount()) + L"  "
          + _num(static_cast<double>(side.Pack.MappedBytes()) / MegaByte) + L" MB  layout=" + _wide(side.LayoutId));
    return true;
}

std::wstring _why(const Convert::Verdict& verdict) {
    return _wide(verdict.Reason()) + (verdict.ByRule ? L" " + verdict.Rule : L"") + L" margin=" + _num(verdict.Margin());
}

void _printVerdict(const Convert::Verdict& v) {
    if (v.SourceLocale.empty()) {
        _line(L"  no language matched");
        return;
    }
    const std::wstring why = _why(v) + L" score=" + _num(v.ScoreOriginal) + L"/" + _num(v.ScoreFixed);
    if (v.WrongLayout) {
        _line(L"  FIX -> " + v.Fixed + L"   [" + _wide(v.SourceLocale) + L" to " + _wide(v.FixedLocale) + L", " + why + L"]");
    } else {
        _line(L"  ok   [" + _wide(v.SourceLocale) + L", " + why + L", alt: " + v.Fixed + L"]");
    }
}

std::vector<Convert::Key> _keysOf(const std::wstring& input, const ReverseMap& reverse) {
    std::vector<Convert::Key> keys;
    for (const wchar_t c : input) {
        const auto it = reverse.find(c);
        keys.push_back(it == reverse.end() ? Convert::Key{0, false, false} : it->second);
    }
    return keys;
}

void _judge(const std::wstring& input, const BoundSide (&sides)[2], const ReplOptions& options) {
    const Convert::Alphabet first(sides[0].Pack.Symbols());
    const Convert::Alphabet second(sides[1].Pack.Symbols());
    const int typedIndex = first.Coverage(input) >= second.Coverage(input) ? 0 : 1;
    const BoundSide& typedSide = sides[typedIndex];
    const BoundSide& otherSide = sides[1 - typedIndex];

    const std::vector<Convert::Key> keys = _keysOf(input, typedSide.Reverse);
    const Convert::Side typed{typedSide.Table.get(), &typedSide.Pack};
    const Convert::Side other{otherSide.Table.get(), &otherSide.Pack};
    const Convert::Rules* rules = options.Rules.Count() ? &options.Rules : nullptr;

    _printVerdict(Convert::Detect(keys, typed, other, options.Threshold, rules, options.Frequency));
    for (size_t n = 2; n <= keys.size(); ++n) {
        const Convert::Verdict early = Convert::Early(std::span(keys).first(n), typed, other, rules, options.Frequency);
        if (early.WrongLayout) {
            _line(L"  early at " + std::to_wstring(n) + L": " + early.Typed + L" -> " + early.Fixed + L"   ["
                  + _why(early) + L"]");
            break;
        }
    }
}

int _runRepl(const Args& args) {
    ReplOptions options;
    if (const int code = _parseRepl(args, options)) {
        return code;
    }
    BoundSide sides[2];
    for (size_t i = 0; i < 2; ++i) {
        if (!_bind(sides[i], options.PackDir, options.Languages[i])) {
            return 1;
        }
    }
    std::wstring input;
    while (Console::ReadLine(input) && !input.empty()) {
        _judge(input, sides, options);
    }
    return 0;
}

struct Command {
    std::string_view Name;
    int (*Run)(const Args&);
};

constexpr Command Commands[] = {{"pack", &_runPack}, {"convert", &_runConvert}, {"rules", &_runRules}};

} // anonymous namespace

int main() {
    Console::Init();
    const Args full = Console::CommandLineUtf8();
    const Args args(full.begin() + (full.empty() ? 0 : 1), full.end());
    if (!args.empty()) {
        if (const auto command = std::ranges::find(Commands, args[0], &Command::Name); command != std::end(Commands)) {
            return command->Run(args);
        }
    }
    return _runRepl(args);
}
